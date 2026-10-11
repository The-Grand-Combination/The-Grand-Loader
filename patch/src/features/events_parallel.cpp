#include "features/events_parallel.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "hooks/memory.h"
#include "log.h"
#include "parallel/worker_pool.h"

// v2game.exe 3.04 (re/systems/events.md, re/systems/parallel-triggers.md):
//
//   DayTick, CALL EventManager_DailyScan (0x008A7CD0, stdcall 1, RET 4) at 0x006861E8.
//   The scan builds one CEventScope on its stack, then
//     odd days ((date - 0x029C55C0) / 24 % 2 == 1): for i in 1..countries-1, c = countries[i], idx = c+0x20;
//       skipped when idx == 0 or countries[idx] has no provinces; scope+0x08 = c+0x1C (tag), +0x0C = idx;
//       for each event e of countries[idx]+0x08..+0x0C (re-read every iteration):
//     even days: for p in 0..g_provinceSource->vt[0x24]()-1, province = world+0xACC[p], skipped when
//       province+0x12C (owner) == 0; scope+0x10 = p; for each event e of province+0x14..+0x18:
//   and per event: if e+0x30 (embedded CAndTrigger).Evaluate(scope): MTTH (0x008AE380), one Mersenne
//   Twister draw, and maybe Event_Fire (0x008A8120, thiscall 2, RET 8; CALLs at 0x008A7EF4 / 0x008A80D5),
//   which runs the event's immediate effects at once.
//
// parallel_events: before the scan, the (trigger, entity) sequence it will evaluate is listed in order and
// the trigger blocks are pointed at a private copy of the CAndTrigger vtable. When the scan calls one of
// them, the next chunk of the sequence is evaluated on the worker pool with copies of the scan's own
// scope, and each call is answered from it as long as it matches the next entry. A fired event changes
// the game, so the results computed before it are dropped and the next chunk is computed afresh. Calls
// that do not match (nested evaluations, a candidate list changed by an effect) run the original code.
// MTTH, the draw and Event_Fire stay on the game thread in the original order. Shadow mode (2) answers
// every call with the original code and compares.

namespace cp::features {

namespace {

constexpr uint32_t kScanCallRva = 0x2861E8;
constexpr uint32_t kScanRva = 0x4A7CD0;
constexpr uint32_t kFireRva = 0x4A8120;
constexpr uint32_t kFireCallRvas[] = {0x4A7EF4, 0x4A80D5};
constexpr uint32_t kAndVtableRva = 0x9F9994;           // CAndTrigger::vftable
constexpr uint32_t kWorldPtrRva = 0xE588E8;
constexpr uint32_t kCountryManagerPtrRva = 0xE587E4;   // +0 count, +4 CCountry* array
constexpr uint32_t kProvinceSourcePtrRva = 0xE5870C;   // object; vt[0x24] = province count
constexpr uint32_t kWorldDate = 0xB0C;
constexpr int32_t kDateBase = 0x029C55C0;
constexpr uint32_t kWorldProvinces = 0xACC;
constexpr uint32_t kCountryTag = 0x1C;
constexpr uint32_t kCountryIndex = 0x20;
constexpr uint32_t kCountryEvents = 0x08;              // vector<CEvent*>
constexpr uint32_t kCountryProvincesBegin = 0x9D8;
constexpr uint32_t kCountryProvincesEnd = 0x9DC;
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kProvinceEvents = 0x14;
constexpr uint32_t kEventTrigger = 0x30;               // embedded CAndTrigger
constexpr uint32_t kScopeTag = 0x08;
constexpr uint32_t kScopeCountry = 0x0C;
constexpr uint32_t kScopeProvince = 0x10;
constexpr size_t kScopeSize = 0x48;                    // what CEventScope::ctor writes and EventScope_Copy copies
constexpr uint32_t kVtableEvaluate = 0x18;
constexpr size_t kVtableSlotsCopied = 16;
constexpr size_t kChunk = 1024;                        // entries evaluated per batch
constexpr int kPerTask = 16;                           // entries per worker task
constexpr int kReportEveryScans = 30;
constexpr int kMaxMismatchLogs = 20;

using EvaluateFn = char(__fastcall*)(void* self, void* edx, void* scope);
using CountFn = int(__fastcall*)(void* self, void* edx);

int g_mode = 0;  // 1 on, 2 shadow
EvaluateFn g_original_evaluate = nullptr;
uintptr_t g_fake_vtable[kVtableSlotsCopied];
uintptr_t g_and_vtable = 0;
uintptr_t g_scan = 0;
uintptr_t g_fire = 0;

struct Entry {
    uintptr_t self;  // the event's trigger block
    uint32_t key;    // country index (country pass) or province index
    uint32_t tag;    // country pass: the tag the scan writes to scope+0x08
    uint8_t result;
};
std::vector<Entry> g_seq;
size_t g_next = 0;        // next entry the scan should ask for
size_t g_ready_end = 0;   // results valid for [g_next, g_ready_end)
bool g_in_scan = false;
bool g_country_pass = false;
volatile uint8_t g_fired = 0;     // an event fired since the current results were computed
volatile int32_t g_fire_depth = 0;
alignas(16) unsigned char g_template[kScopeSize];

// statistics since the last report
uint64_t g_scans = 0, g_entries = 0, g_chunks = 0, g_served = 0, g_passthrough = 0, g_fires = 0;
uint64_t g_compared = 0, g_mismatches = 0, g_batch_ticks = 0;
int g_mismatch_logs = 0;
int64_t g_qpc_freq = 0;

inline uint32_t read32(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(at); }

inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline char evaluate_original(uintptr_t self, void* scope) {
    return g_original_evaluate(reinterpret_cast<void*>(self), nullptr, scope);
}

uintptr_t g_template_origin = 0;  // address of the live scope the template was copied from

// The scope points into itself (CEventScope::ctor sets +0x24, the THIS scope, to the object), so every
// field of the template that points inside the live scope is moved to the same place in the copy.
void fill_scope(unsigned char* scope, const Entry& e) {
    std::memcpy(scope, g_template, kScopeSize);
    uintptr_t copy = reinterpret_cast<uintptr_t>(scope);
    for (size_t off = 0; off < kScopeSize; off += 4) {
        uint32_t value;
        std::memcpy(&value, scope + off, 4);
        if (value >= g_template_origin && value < g_template_origin + kScopeSize) {
            value = static_cast<uint32_t>(value - g_template_origin + copy);
            std::memcpy(scope + off, &value, 4);
        }
    }
    if (g_country_pass) {
        std::memcpy(scope + kScopeTag, &e.tag, 4);
        std::memcpy(scope + kScopeCountry, &e.key, 4);
    } else {
        std::memcpy(scope + kScopeProvince, &e.key, 4);
    }
}

// Worker task: entries [base + index * kPerTask, ...) of the current chunk.
size_t g_chunk_begin = 0, g_chunk_end = 0;
void evaluate_task(int index, void*) {
    size_t begin = g_chunk_begin + static_cast<size_t>(index) * kPerTask;
    size_t end = begin + kPerTask < g_chunk_end ? begin + kPerTask : g_chunk_end;
    alignas(16) unsigned char scope[kScopeSize + 16];
    for (size_t i = begin; i < end; i++) {
        Entry& e = g_seq[i];
        fill_scope(scope, e);
        e.result = evaluate_original(e.self, scope) ? 1 : 0;
    }
}

void compute_chunk(void* live_scope) {
    int64_t start = qpc();
    std::memcpy(g_template, live_scope, kScopeSize);
    g_template_origin = reinterpret_cast<uintptr_t>(live_scope);
    g_chunk_begin = g_next;
    g_chunk_end = g_next + kChunk < g_seq.size() ? g_next + kChunk : g_seq.size();
    int tasks = static_cast<int>((g_chunk_end - g_chunk_begin + kPerTask - 1) / kPerTask);
    parallel::run(tasks, &evaluate_task, nullptr);
    g_ready_end = g_chunk_end;
    g_chunks++;
    g_batch_ticks += static_cast<uint64_t>(qpc() - start);
}

char __fastcall evaluate_hook(void* self, void*, void* scope) {
    if (!g_in_scan || g_fire_depth > 0) {
        return g_original_evaluate(self, nullptr, scope);
    }
    uintptr_t s = reinterpret_cast<uintptr_t>(self);
    uint32_t key = read32(reinterpret_cast<uintptr_t>(scope) + (g_country_pass ? kScopeCountry : kScopeProvince));
    if (g_next >= g_seq.size() || g_seq[g_next].self != s || g_seq[g_next].key != key) {
        g_passthrough++;
        return g_original_evaluate(self, nullptr, scope);
    }
    if (g_fired) {
        g_fired = 0;
        g_ready_end = g_next;  // computed before the event ran its effects
    }
    if (g_next >= g_ready_end) {
        compute_chunk(scope);
    }
    char result = static_cast<char>(g_seq[g_next].result);
    g_next++;
    if (g_mode == 2) {
        char serial = g_original_evaluate(self, nullptr, scope) ? 1 : 0;
        g_compared++;
        if (serial != result) {
            g_mismatches++;
            if (g_mismatch_logs++ < kMaxMismatchLogs) {
                logf("parallel_events: MISMATCH %s pass, %s %u, trigger %08X: serial %d, parallel %d",
                     g_country_pass ? "country" : "province", g_country_pass ? "country" : "province", key, s,
                     serial, result);
            }
        }
        return serial;
    }
    g_served++;
    return result;
}

void add_entry(uintptr_t event, uint32_t key, uint32_t tag) {
    uintptr_t block = event + kEventTrigger;
    uintptr_t vtable = read32(block);
    if (vtable == g_and_vtable) {
        *reinterpret_cast<uintptr_t*>(block) = reinterpret_cast<uintptr_t>(g_fake_vtable);
    } else if (vtable != reinterpret_cast<uintptr_t>(g_fake_vtable)) {
        return;  // not a plain CAndTrigger block: the scan's call will not reach the hook
    }
    g_seq.push_back(Entry{block, key, tag, 0});
}

// Lists, in the scan's order, every trigger it will evaluate today (as long as nothing changes).
void __cdecl scan_begin() {
    g_seq.clear();
    g_next = g_ready_end = 0;
    g_fired = 0;
    uintptr_t world = read32(hooks::exe_addr(kWorldPtrRva));
    uintptr_t manager = read32(hooks::exe_addr(kCountryManagerPtrRva));
    if (!world || !manager) {
        return;
    }
    int32_t days = (static_cast<int32_t>(read32(world + kWorldDate)) - kDateBase) / 24;
    g_country_pass = days % 2 == 1;
    uintptr_t countries = read32(manager + 4);
    if (g_country_pass) {
        int32_t count = static_cast<int32_t>(read32(manager));
        for (int32_t i = 1; i < count; i++) {
            uintptr_t c = read32(countries + i * 4);
            uint32_t index = read32(c + kCountryIndex);
            if (index == 0) {
                continue;
            }
            uintptr_t owner = read32(countries + index * 4);
            if (static_cast<int32_t>((read32(owner + kCountryProvincesEnd) - read32(owner + kCountryProvincesBegin)) &
                                     0xFFFFFFFCu) <= 0) {
                continue;
            }
            uintptr_t begin = read32(owner + kCountryEvents), end = read32(owner + kCountryEvents + 4);
            for (uintptr_t it = begin; it < end; it += 4) {
                add_entry(read32(it), index, read32(c + kCountryTag));
            }
        }
    } else {
        uintptr_t source = read32(hooks::exe_addr(kProvinceSourcePtrRva));
        if (!source) {
            return;
        }
        auto count_fn = reinterpret_cast<CountFn>(read32(read32(source) + 0x24));
        int32_t count = count_fn(reinterpret_cast<void*>(source), nullptr);
        uintptr_t provinces = read32(world + kWorldProvinces);
        for (int32_t p = 0; p < count; p++) {
            uintptr_t province = read32(provinces + p * 4);
            if (read32(province + kProvinceOwner) == 0) {
                continue;
            }
            uintptr_t begin = read32(province + kProvinceEvents), end = read32(province + kProvinceEvents + 4);
            for (uintptr_t it = begin; it < end; it += 4) {
                add_entry(read32(it), static_cast<uint32_t>(p), 0);
            }
        }
    }
    g_entries += g_seq.size();
    g_in_scan = true;
}

void __cdecl scan_end() {
    g_in_scan = false;
    if (++g_scans % kReportEveryScans != 0) {
        return;
    }
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    double ms_day = g_qpc_freq ? 1000.0 * static_cast<double>(g_batch_ticks) / g_qpc_freq / kReportEveryScans : 0.0;
    logf("parallel_events: last %d scans: %llu triggers/day listed, %llu served, %llu passed through, %llu chunks, "
         "%llu events fired, batches %.2f ms/day (%d workers + game thread)",
         kReportEveryScans, ull(g_entries / kReportEveryScans), ull(g_served / kReportEveryScans),
         ull(g_passthrough / kReportEveryScans), ull(g_chunks), ull(g_fires), ms_day, parallel::workers());
    if (g_mode == 2) {
        logf("parallel_events: shadow: %llu compared, %llu mismatches", ull(g_compared), ull(g_mismatches));
    }
    g_entries = g_served = g_passthrough = g_chunks = g_fires = g_compared = g_mismatches = g_batch_ticks = 0;
}

void __cdecl fire_done() {
    g_fired = 1;
    g_fires++;
}

// DayTick's call: list the sequence, run the scan, close it. Registers and flags are kept around both helpers.
__declspec(naked) void scan_thunk() {
    __asm {
        pushad
        pushfd
        call scan_begin
        popfd
        popad
        push dword ptr [esp + 4]
        call dword ptr [g_scan]
        pushad
        pushfd
        call scan_end
        popfd
        popad
        ret 4
    }
}

// The scan's Event_Fire calls (ECX = event, two stack arguments). Evaluations made while the event runs
// pass through; afterwards the results computed before it are dropped.
__declspec(naked) void fire_thunk() {
    __asm {
        inc dword ptr [g_fire_depth]
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call dword ptr [g_fire]
        dec dword ptr [g_fire_depth]
        pushad
        pushfd
        call fire_done
        popfd
        popad
        ret 8
    }
}

}  // namespace

void install_events_parallel(const Config& config) {
    g_mode = config.parallel_events;
    if (g_mode != 1 && g_mode != 2) {
        logf("parallel_events: disabled");
        return;
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;
    g_and_vtable = hooks::exe_addr(kAndVtableRva);
    g_scan = hooks::exe_addr(kScanRva);
    g_fire = hooks::exe_addr(kFireRva);
    std::memcpy(g_fake_vtable, reinterpret_cast<const void*>(g_and_vtable), sizeof(g_fake_vtable));
    g_original_evaluate = reinterpret_cast<EvaluateFn>(g_fake_vtable[kVtableEvaluate / 4]);
    if (!hooks::expect_bytes(reinterpret_cast<uintptr_t>(g_original_evaluate), "\x55\x8B\xEC", 3) ||
        !hooks::expect_bytes(hooks::exe_addr(kScanCallRva), "\xE8", 1)) {
        logf("parallel_events: CAndTrigger::Evaluate or the scan call not recognised, feature off");
        return;
    }
    for (uint32_t site : kFireCallRvas) {
        if (!hooks::expect_bytes(hooks::exe_addr(site), "\xE8", 1)) {
            logf("parallel_events: Event_Fire call %08X not recognised, feature off", site + 0x400000);
            return;
        }
    }
    g_fake_vtable[kVtableEvaluate / 4] = reinterpret_cast<uintptr_t>(&evaluate_hook);
    g_seq.reserve(1 << 16);
    // Event_Fire first: with the scan hooked but not these, a fired event would leave stale results.
    for (uint32_t site : kFireCallRvas) {
        if (!hooks::redirect_call(hooks::exe_addr(site), g_fire, reinterpret_cast<uintptr_t>(&fire_thunk))) {
            logf("parallel_events: Event_Fire call %08X does not call Event_Fire or is already hooked, feature off",
                 site + 0x400000);
            return;  // a redirected fire site alone only marks results stale, which nothing reads
        }
    }
    if (!hooks::redirect_call(hooks::exe_addr(kScanCallRva), g_scan, reinterpret_cast<uintptr_t>(&scan_thunk))) {
        logf("parallel_events: scan call site already hooked (tick_profiler?), feature off");
        return;
    }
    logf("parallel_events: active (%s): the daily event scan's triggers are evaluated on worker threads",
         g_mode == 2 ? "shadow mode, the game still uses its own evaluation" : "results used");
}

}  // namespace cp::features
