#include "features/profilers/sample_profiler.h"

#include <windows.h>
#include <timeapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hooks/hourly_tick.h"
#include "hooks/memory.h"
#include "log.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;  // this DLL's own base (MSVC linker symbol)

// How it works. The sampler thread is started from the first hourly tick (on the game thread, outside
// the loader lock, which also tells us which thread to sample). Every `interval` ms, while the game date
// has changed in the last two seconds (not paused, not in a menu), it:
//
//   1. suspends the game thread, reads EIP/ESP, copies up to 16 KB of its stack, resumes it. Nothing
//      else happens while the thread is suspended: no allocation, no lock, so it cannot deadlock on a
//      heap or loader lock the game thread holds;
//   2. walks the copy for return addresses into the exe (a value right after a CALL). A `CALL rel32` is
//      only accepted when its target contains the younger frame's address, which drops most stale
//      return addresses left in locals; indirect calls (CALL EAX, CALL [reg+n]) cannot be checked and are
//      accepted;
//   3. counts the whole stack (leaf + return addresses) as one entry of a collapsed-stack table.
//
// Addresses are written as static VAs of the 3.04 listing (image base 0x00400000): the exe is relocated
// by ASLR at runtime, so runtime addresses would not match docs/re/decomp. Each sample is weighted by the real
// time since the previous one. Totals are cumulative from the first game day (reset when the date jumps,
// i.e. a load) and rewritten to performance_patch_samples.tsv every 30 days.

namespace cp::features {

namespace {

constexpr uint32_t kWorldPtrRva = 0xE588E8;  // DAT_012588E8
constexpr uint32_t kWorldDateOffset = 0xB0C;  // hours
constexpr uint32_t kTextBeginRva = 0x001000;  // .text of v2game.exe 3.04
constexpr uint32_t kTextEndRva = 0x88911A;
constexpr uint32_t kListingBase = 0x00400000;
constexpr uint32_t kMaxFunctionSpan = 0x40000;  // a CALL rel32 target must lie this close below the callee frame
constexpr int32_t kReportEveryDays = 30;
constexpr double kPauseGapMs = 2000.0;
constexpr size_t kStackCopyBytes = 16 * 1024;
constexpr int kMaxFrames = 96;
constexpr uint32_t kStackBits = 19;  // 512 k slots (16 MB), used up to 3/4: a 24-year run reached 98 k distinct stacks and dropped 30 % with 2^17
constexpr uint32_t kStackSlots = 1u << kStackBits;
constexpr size_t kPoolWords = 12u << 20;  // 48 MB of frames (~300 k stacks of ~40 frames)
constexpr int kMaxModules = 64;
constexpr uint32_t kModuleTag = 0xF0000000u;  // leaf in another module: kModuleTag | module index
constexpr uint32_t kUnknownLeaf = 0xFFFFFFFFu;
constexpr uint32_t kOwnTag = 0xE0000000u;      // leaf in performance_patch.dll: kOwnTag | RVA (resolve with its .map)

struct StackEntry {
    uint64_t hash;
    uint32_t offset;  // into g_pool
    uint32_t count;   // frames
    uint32_t samples;
    uint64_t ticks;
};

StackEntry* g_stacks = nullptr;  // kStackSlots entries
uint32_t* g_pool = nullptr;      // kPoolWords frames
uint32_t g_stack_count = 0;
size_t g_pool_used = 0;
uint64_t g_dropped = 0;
uintptr_t g_modules[kMaxModules];
int g_module_count = 0;

int g_interval_ms = 1;
HANDLE g_game_thread = nullptr;
uintptr_t g_stack_lo = 0;
uintptr_t g_stack_hi = 0;
uintptr_t g_exe_base = 0;
uintptr_t g_text_begin = 0;
uintptr_t g_text_end = 0;
std::wstring g_report_path;
bool g_started = false;

int64_t g_qpc_freq = 0;
int32_t g_first_day = -1;
int32_t g_last_day = -1;
int32_t g_last_report_day = -1;
int32_t g_days = 0;  // day changes seen since g_first_day
int64_t g_last_day_change_qpc = 0;
uint64_t g_samples = 0;
uint64_t g_sampled_ticks = 0;

inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline double ms(uint64_t ticks) { return 1000.0 * static_cast<double>(ticks) / static_cast<double>(g_qpc_freq); }

// Called from the sampler thread: the world may be torn down between the two reads.
int32_t current_day() {
    __try {
        uintptr_t world = *reinterpret_cast<const uint32_t*>(hooks::exe_addr(kWorldPtrRva));
        return world ? static_cast<int32_t>(*reinterpret_cast<const uint32_t*>(world + kWorldDateOffset)) / 24 : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void clear_exports();

void reset(int32_t day) {
    memset(g_stacks, 0, sizeof(StackEntry) * kStackSlots);
    g_stack_count = 0;
    g_pool_used = 0;
    g_dropped = 0;
    clear_exports();
    g_module_count = 0;
    g_samples = 0;
    g_sampled_ticks = 0;
    g_first_day = day;
    g_last_report_day = day;
    g_days = 0;
}

inline bool in_text(uintptr_t v) { return v >= g_text_begin && v < g_text_end; }
inline uint32_t to_va(uintptr_t runtime) { return static_cast<uint32_t>(runtime - g_exe_base + kListingBase); }

// CALL r/m32 (FF /2) ending right before `p`: register / [reg] (2 bytes), [reg+disp8] / SIB (3),
// disp32 forms (6, 7).
bool after_indirect_call(const uint8_t* p) {
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10 && (p[-1] & 0xC0) != 0x40 && (p[-1] & 0xC0) != 0x80) {
        return true;
    }
    return (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) ||
           (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10);
}

// Accepts `ret` as the caller frame of `inner` (the younger frame's address, runtime).
bool is_caller_frame(uintptr_t ret, uintptr_t inner) {
    if (!in_text(ret) || ret < g_text_begin + 7) {
        return false;
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ret);
    if (p[-5] == 0xE8) {
        int32_t rel;
        memcpy(&rel, p - 4, sizeof(rel));
        uintptr_t target = ret + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
        if (in_text(inner)) {
            if (inner >= target && inner - target < kMaxFunctionSpan) {
                return true;
            }
        } else if (in_text(target) && target + 2 <= g_text_end &&
                   reinterpret_cast<const uint8_t*>(target)[0] == 0xFF &&
                   reinterpret_cast<const uint8_t*>(target)[1] == 0x25) {
            return true;  // leaf in another module (ntdll, d3d9, ...): reached through a JMP [import] thunk
        }
    }
    return after_indirect_call(p);
}

// Exports of a module sorted by address, so a leaf in ntdll/kernel32/d3d9 can be named
// (RtlAllocateHeap, NtDelayExecution, ...). Built on first use, on the sampler thread.
struct Export {
    uint32_t rva;
    const char* name;
};
std::vector<Export> g_exports[kMaxModules];

void clear_exports() {
    for (int i = 0; i < g_module_count; i++) {
        g_exports[i].clear();
    }
}

void load_exports(int index) {
    auto base = reinterpret_cast<const uint8_t*>(g_modules[index]);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !dir.Size) {
        return;
    }
    auto exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto functions = reinterpret_cast<const uint32_t*>(base + exp->AddressOfFunctions);
    auto names = reinterpret_cast<const uint32_t*>(base + exp->AddressOfNames);
    auto ordinals = reinterpret_cast<const uint16_t*>(base + exp->AddressOfNameOrdinals);
    std::vector<Export>& out = g_exports[index];
    for (uint32_t i = 0; i < exp->NumberOfNames; i++) {
        uint32_t rva = functions[ordinals[i]];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) {
            continue;  // forwarder string, not code
        }
        out.push_back({rva, reinterpret_cast<const char*>(base + names[i])});
    }
    std::sort(out.begin(), out.end(), [](const Export& a, const Export& b) { return a.rva < b.rva; });
}

// kModuleTag | module index << 16 | (export index + 1), 0 in the low half when no export precedes it.
uint32_t module_leaf(uintptr_t eip) {
    HMODULE h = nullptr;
    // The handle is kept (no UNCHANGED_REFCOUNT): the export names point into the module image.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(eip), &h) ||
        !h) {
        return kUnknownLeaf;
    }
    uintptr_t base = reinterpret_cast<uintptr_t>(h);
    if (h == reinterpret_cast<HMODULE>(&__ImageBase)) {
        return kOwnTag | static_cast<uint32_t>(eip - base);  // our own code: keep the offset, not an export
    }
    int index = -1;
    for (int i = 0; i < g_module_count; i++) {
        if (g_modules[i] == base) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        if (g_module_count >= kMaxModules) {
            return kUnknownLeaf;
        }
        index = g_module_count++;
        g_modules[index] = base;
        load_exports(index);
    }
    const std::vector<Export>& exports = g_exports[index];
    uint32_t rva = static_cast<uint32_t>(eip - base);
    auto it = std::upper_bound(exports.begin(), exports.end(), rva,
                               [](uint32_t v, const Export& e) { return v < e.rva; });
    uint32_t export_id = it == exports.begin() ? 0 : static_cast<uint32_t>(it - exports.begin());  // index + 1
    if (export_id > 0xFFFF) {
        export_id = 0;
    }
    return kModuleTag | static_cast<uint32_t>(index) << 16 | export_id;
}

void add_stack(const uint32_t* frames, int count, uint64_t ticks) {
    uint64_t hash = 1469598103934665603ull;  // FNV-1a over the frames
    for (int i = 0; i < count; i++) {
        hash = (hash ^ frames[i]) * 1099511628211ull;
    }
    if (hash == 0) {
        hash = 1;
    }
    for (uint32_t i = 0; i < kStackSlots; i++) {
        StackEntry& e = g_stacks[(static_cast<uint32_t>(hash) + i) & (kStackSlots - 1)];
        if (e.hash == hash && e.count == static_cast<uint32_t>(count) &&
            memcmp(g_pool + e.offset, frames, count * sizeof(uint32_t)) == 0) {
            e.samples++;
            e.ticks += ticks;
            return;
        }
        if (e.hash == 0) {
            if (g_stack_count >= kStackSlots / 4 * 3 || g_pool_used + count > kPoolWords) {
                break;
            }
            e.hash = hash;
            e.offset = static_cast<uint32_t>(g_pool_used);
            e.count = static_cast<uint32_t>(count);
            e.samples = 1;
            e.ticks = ticks;
            memcpy(g_pool + g_pool_used, frames, count * sizeof(uint32_t));
            g_pool_used += count;
            g_stack_count++;
            return;
        }
    }
    g_dropped++;
}

// `esp`/`ebp` are the game thread's registers; `stack` holds `bytes` bytes copied from `esp`.
void record(uintptr_t eip, uintptr_t esp, uintptr_t ebp, const uint8_t* stack, size_t bytes, uint64_t ticks) {
    g_samples++;
    g_sampled_ticks += ticks;
    const uint32_t* words = reinterpret_cast<const uint32_t*>(stack);
    size_t nwords = bytes / 4;

    // EBP chain: every [EBP+4] on it is a true return address. Windows DLLs and the game's functions with
    // C++ exception frames keep EBP frames, so the chain anchors the walk and stale return addresses
    // left in uninitialised locals cannot build a false chain across it.
    static bool anchor[kStackCopyBytes / 4];
    size_t anchors[kMaxFrames];
    int nanchors = 0;
    // `e - esp` cannot overflow once e >= esp; `e + 8` could (a chain value near 0xFFFFFFFF wrapped
    // around, passed the bound and wrote anchor[] far out of range: it crashed).
    for (uintptr_t e = ebp; bytes >= 8 && e >= esp && e - esp <= bytes - 8 && nanchors < kMaxFrames;) {
        size_t slot = (e + 4 - esp) / 4;
        anchor[slot] = true;
        anchors[nanchors++] = slot;
        uintptr_t next = words[(e - esp) / 4];
        if (next <= e) {
            break;
        }
        e = next;
    }

    uint32_t frames[kMaxFrames];
    int count = 0;
    frames[count++] = in_text(eip) ? to_va(eip) : module_leaf(eip);
    uintptr_t inner = eip;
    for (size_t i = 0; i < nwords && count < kMaxFrames; i++) {
        uintptr_t v = words[i];
        if (anchor[i]) {
            // An exe function without a frame pointer may use EBP as a plain register, so an anchor into
            // the exe must still sit right after a CALL.
            if (!in_text(v)) {
                inner = v;  // a DLL frame: the next exe frame is the one that called into that DLL
            } else if (v >= g_text_begin + 7 && (reinterpret_cast<const uint8_t*>(v)[-5] == 0xE8 ||
                                                 after_indirect_call(reinterpret_cast<const uint8_t*>(v)))) {
                frames[count++] = to_va(v);
                inner = v;
            }
        } else if (is_caller_frame(v, inner)) {
            frames[count++] = to_va(v);
            inner = v;
        }
    }
    for (int k = 0; k < nanchors; k++) {
        anchor[anchors[k]] = false;
    }
    add_stack(frames, count, ticks);
}

void report(int32_t day) {
    int32_t days = g_days;
    if (days <= 0) {
        return;
    }
    FILE* out = nullptr;
    if (_wfopen_s(&out, g_report_path.c_str(), L"w") != 0 || !out) {
        logf("sample_profiler: cannot write performance_patch_samples.tsv");
        return;
    }
    fprintf(out, "# performance_patch sample profile v3, v2game.exe 3.04 (runtime base %08X, written as listing VAs), interval %d ms, "
                 "%d days (day %d to %d, year %d), %llu samples, %.1f ms/day sampled, %u stacks, %llu dropped\n",
            static_cast<unsigned>(g_exe_base), g_interval_ms, days, g_first_day, day, day / 365 - 5000,
            static_cast<unsigned long long>(g_samples), ms(g_sampled_ticks) / days, g_stack_count,
            static_cast<unsigned long long>(g_dropped));
    for (int i = 0; i < g_module_count; i++) {
        wchar_t wide[MAX_PATH] = {};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(g_modules[i]), wide, MAX_PATH);
        const wchar_t* name = wcsrchr(wide, L'\\');
        char narrow[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, name ? name + 1 : wide, -1, narrow, sizeof(narrow), nullptr, nullptr);
        fprintf(out, "mod\tm%d\t%s\n", i, narrow[0] ? narrow : "?");
    }
    // stack <samples> <ms/day> <leaf;caller;caller;...>, leaf first; a leaf `m<i>` is module i, `?` unknown.
    for (uint32_t s = 0; s < kStackSlots; s++) {
        const StackEntry& e = g_stacks[s];
        if (!e.hash) {
            continue;
        }
        fprintf(out, "stack\t%u\t%.4f\t", e.samples, ms(e.ticks) / days);
        for (uint32_t k = 0; k < e.count; k++) {
            uint32_t f = g_pool[e.offset + k];
            if (k) {
                fputc(';', out);
            }
            if ((f & 0xF0000000u) == kOwnTag) {
                fprintf(out, "cp+0x%X", f & 0x0FFFFFFFu);
            } else if (f == kUnknownLeaf) {
                fputc('?', out);
            } else if ((f & 0xF0000000u) == kModuleTag) {
                uint32_t module = (f >> 16) & 0x0FFFu;
                uint32_t export_id = f & 0xFFFFu;
                if (export_id && module < kMaxModules && export_id <= g_exports[module].size()) {
                    fprintf(out, "m%u:%s", module, g_exports[module][export_id - 1].name);
                } else {
                    fprintf(out, "m%u", module);
                }
            } else {
                fprintf(out, "0x%08X", f);
            }
        }
        fputc('\n', out);
    }
    fclose(out);
    logf("sample_profiler: %d days (year %d), %llu samples, %.1f ms/day sampled, %u stacks -> performance_patch_samples.tsv", days,
         day / 365 - 5000, static_cast<unsigned long long>(g_samples), ms(g_sampled_ticks) / days, g_stack_count);
}

DWORD WINAPI sampler_main(void*) {
    timeBeginPeriod(1);
    static uint8_t stack[kStackCopyBytes];
    int64_t last = qpc();
    int64_t max_weight = g_qpc_freq * 50 / 1000;  // a sample never stands for more than 50 ms
    for (;;) {
        Sleep(static_cast<DWORD>(g_interval_ms));
        int64_t now = qpc();
        int64_t weight = now - last;
        last = now;
        if (weight > max_weight) {
            weight = max_weight;
        }

        int32_t day = current_day();
        if (day < 0) {
            continue;
        }
        if (day != g_last_day) {
            if (g_first_day < 0 || day < g_last_day || day > g_last_day + 5) {
                reset(day);  // first day, or a load
            } else {
                g_days += day - g_last_day;
                if (day - g_last_report_day >= kReportEveryDays) {
                    report(day);
                    g_last_report_day = day;
                }
            }
            g_last_day = day;
            g_last_day_change_qpc = now;
        }
        if (ms(static_cast<uint64_t>(now - g_last_day_change_qpc)) > kPauseGapMs) {
            continue;  // paused, menu, save: not part of the day's cost
        }

        if (SuspendThread(g_game_thread) == static_cast<DWORD>(-1)) {
            continue;
        }
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_CONTROL;
        bool ok = GetThreadContext(g_game_thread, &ctx) != 0;  // also waits for the suspension
        size_t bytes = 0;
        if (ok && ctx.Esp >= g_stack_lo && ctx.Esp < g_stack_hi) {
            bytes = g_stack_hi - ctx.Esp;
            if (bytes > kStackCopyBytes) {
                bytes = kStackCopyBytes;
            }
            memcpy(stack, reinterpret_cast<const void*>(static_cast<uintptr_t>(ctx.Esp)), bytes);
        }
        ResumeThread(g_game_thread);
        if (ok) {
            record(ctx.Eip, ctx.Esp, ctx.Ebp, stack, bytes, static_cast<uint64_t>(weight));
        }
    }
}

void start_on_game_thread() {
    if (g_started) {
        return;
    }
    g_started = true;
    g_stacks = static_cast<StackEntry*>(
        VirtualAlloc(nullptr, sizeof(StackEntry) * kStackSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_pool = static_cast<uint32_t*>(
        VirtualAlloc(nullptr, sizeof(uint32_t) * kPoolWords, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_stacks || !g_pool) {
        logf("sample_profiler: cannot allocate the stack tables, feature off");
        return;
    }
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_thread,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
        logf("sample_profiler: cannot open the game thread, feature off");
        return;
    }
    const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    g_stack_hi = reinterpret_cast<uintptr_t>(tib->StackBase);
    MEMORY_BASIC_INFORMATION mbi = {};
    VirtualQuery(tib->StackLimit, &mbi, sizeof(mbi));
    g_stack_lo = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
    HANDLE t = CreateThread(nullptr, 0, &sampler_main, nullptr, 0, nullptr);
    if (!t) {
        logf("sample_profiler: cannot create the sampler thread, feature off");
        return;
    }
    SetThreadPriority(t, THREAD_PRIORITY_HIGHEST);
    CloseHandle(t);
    logf("sample_profiler: sampling thread %lu every %d ms (exe at %08X, stack %08X-%08X)", GetCurrentThreadId(),
         g_interval_ms, static_cast<unsigned>(g_exe_base), static_cast<unsigned>(g_stack_lo),
         static_cast<unsigned>(g_stack_hi));
}

}  // namespace

void install_sample_profiler(const Config& config) {
    if (config.sample_profiler <= 0) {
        logf("sample_profiler: disabled");
        return;
    }
    g_interval_ms = config.sample_profiler > 100 ? 100 : config.sample_profiler;
    if (g_interval_ms != config.sample_profiler) {
        logf("sample_profiler: interval capped at 100 ms");
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;
    g_exe_base = hooks::exe_base();
    g_text_begin = hooks::exe_addr(kTextBeginRva);
    g_text_end = hooks::exe_addr(kTextEndRva);
    g_report_path = std::wstring(host().game_dir) + L"\\performance_patch_samples.tsv";
    if (!hooks::on_hourly_tick(&start_on_game_thread)) {
        logf("sample_profiler: hourly tick hook unavailable, feature off");
        return;
    }
    logf("sample_profiler: will start on the first game tick, every %d ms -> performance_patch_samples.tsv every %d game days",
         g_interval_ms, kReportEveryDays);
}

}  // namespace cp::features
