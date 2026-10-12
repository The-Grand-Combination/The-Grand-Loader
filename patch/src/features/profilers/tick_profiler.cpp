#include "features/profilers/tick_profiler.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "features/profilers/tick_profiler_sites.h"
#include "hooks/memory.h"
#include "log.h"

// Daily tick of v2game.exe 3.04 (docs/re/systems/tick.md):
//
//   Update_World_and_Advance_Time (0x00682EC0)            <- 0x0072ECFA
//     DayTick (0x006859C0)                                 <- 0x00683AB1
//       per province: Province_DailyUpdate (0x004B1A10)    <- 0x00685B49
//       per country:  Country_DailyUpdate (0x005091A0)     <- 0x00685ED1
//                       AI_DailyUpdate (0x0085CCB0)         <- 0x0050B0EB (also via FUN_0085CF10)
//                       FUN_0085CF10                        <- 0x0050B0F0
//                       FUN_00519F80                        <- 0x0050B0F6
//       EventManager_DailyScan (0x008A7CD0)                <- 0x006861E8
//       + every other game-code call inside DayTick (level 2, tick_profiler_sites.h)
//
// Each call site is redirected to a thunk. The thunk records the time, swaps the return address for
// a stub that records the end, and jumps to the original function with every register and the stack
// exactly as the caller left them, so the game's custom calling conventions never matter.

namespace {

struct Known {
    uint32_t target_rva;
    const char* name;
};
constexpr Known kKnown[] = {
    {0x282EC0, "world_update"},  {0x2859C0, "day_tick"},     {0x0B1A10, "provinces"},    {0x1091A0, "countries"},
    {0x45CCB0, "ai"},            {0x45CF10, "country_ai2"},  {0x119F80, "country_post"}, {0x4A7CD0, "events"},
    {0x285620, "time_throttle"}, {0x5B7700, "rng_refill"}, {0x089990, "economy_update"},
    {0x108590, "country_prepass"}, {0x254D80, "frame_update"}, {0x282BD0, "time_advance"},
    {0x5DF2B0, "message_pump"}, {0x084060, "world_market"}, {0x0808D0, "goods_distribution"},
    {0x082FF0, "wm_factory_step"}, {0x085E40, "wm_country_step"},
};

struct Level1 {
    uint32_t site_rva;
    uint32_t target_rva;
};
constexpr Level1 kLevel1[] = {
    {0x32ECFA, 0x282EC0}, {0x283AB1, 0x2859C0}, {0x285B49, 0x0B1A10}, {0x285ED1, 0x1091A0},
    {0x10B0EB, 0x45CCB0}, {0x10B0F0, 0x45CF10}, {0x10B0F6, 0x119F80}, {0x2861E8, 0x4A7CD0},
};

constexpr int kMaxSites = 600;
constexpr int kMaxDepth = 64;
constexpr int32_t kReportEveryDays = 30;
constexpr uint32_t kWorldPtrRva = 0xE588E8;
constexpr uint32_t kWorldDateOffset = 0xB0C;

struct Frame {
    int target;
    uintptr_t ret;
    uintptr_t ret_slot;  // ESP at callee entry (address of the return-address slot); younger frames are lower
    int64_t start;
    int64_t child_ticks;
    bool nested_frame;   // a frame_update entered while world_update is running (rendered by the throttle)
};
Frame g_shadow[kMaxDepth];
int g_depth = 0;
bool g_lost_frames = false;

struct Target {
    uint32_t rva;
    char name[24];
    uint64_t calls = 0;
    uint64_t inclusive = 0;
    uint64_t exclusive = 0;
    int active = 0;  // instances on the shadow stack; inclusive time is only added by the outermost one
};
std::vector<Target> g_targets;
uintptr_t g_site_target_addr[kMaxSites];  // per site id: absolute address to jump to
int g_site_target_index[kMaxSites];       // per site id: index into g_targets
int g_site_count = 0;
struct Added {
    uint32_t rva;      // call site, or vtable slot
    uint32_t target;
    bool slot;         // true: patch the pointer at rva instead of a CALL rel32
};
std::vector<Added> g_sites_added;  // in id order
int g_world_target = -1;
int g_frame_target = -1;
uint64_t g_nested_frames = 0;  // frames rendered from inside world_update (UpdateTimeThrottle)
uint64_t g_nested_frame_ticks = 0;
int64_t g_qpc_freq = 0;
int32_t g_window_start_day = -1;
int32_t g_last_day = -1;
int64_t g_window_start_qpc = 0;
int g_days_in_window = 0;

inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int32_t current_day() {
    uintptr_t world = *reinterpret_cast<const uint32_t*>(cp::hooks::exe_addr(kWorldPtrRva));
    return world ? static_cast<int32_t>(*reinterpret_cast<const uint32_t*>(world + kWorldDateOffset)) / 24 : -1;
}

void report() {
    int days = g_days_in_window;
    if (days <= 0) {
        return;
    }
    auto ms = [](uint64_t ticks) {
        return g_qpc_freq ? 1000.0 * static_cast<double>(ticks) / static_cast<double>(g_qpc_freq) : 0.0;
    };
    double wall = ms(static_cast<uint64_t>(qpc() - g_window_start_qpc)) / days;
    double inside = g_world_target >= 0 ? ms(g_targets[g_world_target].inclusive) / days : 0.0;
    double frame = 0.0;
    double frames = 0.0;
    if (g_frame_target >= 0) {
        frame = ms(g_targets[g_frame_target].inclusive) / days;
        frames = static_cast<double>(g_targets[g_frame_target].calls) / days;
    }
    // Year as the game shows it: 365-day years counted from -5000.
    int year = g_window_start_day / 365 - 5000;
    cp::logf("tick_profiler: year %d, days %d-%d, %d days, wall %.1f ms/day; frame_update %.1f ms/day over %.1f frames/day "
             "(%.1f ms/day of it is world_update); outside frame_update %.1f ms/day (message pump, present, idle)",
             year, g_window_start_day, g_last_day, days, wall, frame, frames, inside,
             wall - frame);
    cp::logf("tick_profiler: %.1f frames/day rendered from inside world_update (UpdateTimeThrottle), %.2f ms/day",
             static_cast<double>(g_nested_frames) / days, ms(g_nested_frame_ticks) / days);
    cp::logf("tick_profiler:   %-22s %9s %9s %10s", "block", "incl ms", "excl ms", "calls/day");
    std::vector<size_t> order(g_targets.size());
    for (size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [](size_t a, size_t b) { return g_targets[a].exclusive > g_targets[b].exclusive; });
    for (size_t i : order) {
        const Target& t = g_targets[i];
        if (t.calls == 0) {
            continue;
        }
        cp::logf("tick_profiler:   %-22s %9.2f %9.2f %10llu", t.name, ms(t.inclusive) / days, ms(t.exclusive) / days,
                 static_cast<unsigned long long>(t.calls / days));
    }
    if (g_lost_frames) {
        cp::logf("tick_profiler: frames were lost at least once in this window (exception unwind); exclusive times may be off");
        g_lost_frames = false;
    }
}

void on_day(int32_t day) {
    if (day == g_last_day) {
        return;
    }
    // The first window also starts with a reset: until the first game day the counters hold menu and
    // loading frames, which made the first report claim 24 frames/day and a negative "outside" time.
    bool first = g_window_start_day < 0;
    if (first || day - g_window_start_day >= kReportEveryDays) {
        if (!first) {
            report();
        }
        for (Target& t : g_targets) {
            t.calls = t.inclusive = t.exclusive = 0;  // `active` describes the live stack: kept
        }
        g_nested_frames = g_nested_frame_ticks = 0;
        g_window_start_day = day;
        g_window_start_qpc = qpc();
        g_days_in_window = 0;
    }
    g_days_in_window++;
    g_last_day = day;
}

}  // namespace

// Called from the thunks; plain integer code only (the thunks save XMM/flags/registers around it).
// Pops the shadow stack down to `depth` without attributing the dropped frames.
void drop_frames(int depth) {
    while (g_depth > depth) {
        g_depth--;
        g_targets[g_shadow[g_depth].target].active--;
        g_lost_frames = true;
    }
}

extern "C" uintptr_t __cdecl pp_profiler_enter(int site, uintptr_t ret, uintptr_t ret_slot) {
    int target = g_site_target_index[site];
    // world_update runs inside frame_update (and frame_update can run again inside it), so the day is
    // checked on every outermost world_update rather than at depth 0.
    if (target == g_world_target && g_targets[target].active == 0) {
        int32_t day = current_day();
        if (day >= 0) {
            on_day(day);
        }
    }
    // Frames above us on the shadow stack whose return slot is below ours were abandoned (exception
    // unwind or longjmp past our return stub). Drop them; their time is not attributed.
    int keep = g_depth;
    while (keep > 0 && g_shadow[keep - 1].ret_slot <= ret_slot) {
        keep--;
    }
    drop_frames(keep);
    if (g_depth >= kMaxDepth) {
        drop_frames(kMaxDepth - 1);
    }
    Frame& f = g_shadow[g_depth++];
    f.target = target;
    f.ret = ret;
    f.ret_slot = ret_slot;
    f.child_ticks = 0;
    f.nested_frame = target == g_frame_target && g_world_target >= 0 && g_targets[g_world_target].active > 0;
    g_targets[target].active++;
    f.start = qpc();
    return g_site_target_addr[site];
}

// `esp_now` is the stack pointer at return-stub entry: ret_slot + 4 + bytes the callee cleaned.
extern "C" uintptr_t __cdecl pp_profiler_leave(uintptr_t esp_now) {
    int64_t now = qpc();
    // Our frame is the deepest one whose return slot lies below esp_now; anything above it on the
    // shadow stack with a lower slot was abandoned.
    int k = g_depth;
    while (k > 0 && g_shadow[k - 1].ret_slot < esp_now) {
        k--;
    }
    if (k == g_depth) {
        g_lost_frames = true;  // nothing matches below esp_now; best effort: the most recent frame
        if (g_depth == 0) {
            return 0;  // no frame at all; unrecoverable
        }
        k = g_depth - 1;
    }
    drop_frames(k + 1);
    g_depth = k;
    Frame& f = g_shadow[g_depth];
    uint64_t elapsed = static_cast<uint64_t>(now - f.start);
    Target& t = g_targets[f.target];
    t.calls++;
    // A re-entered target (frame_update inside world_update inside frame_update) would otherwise count
    // the inner time twice in its inclusive total.
    if (--t.active == 0) {
        t.inclusive += elapsed;
    }
    t.exclusive += elapsed - static_cast<uint64_t>(f.child_ticks);
    if (f.nested_frame) {
        g_nested_frames++;
        g_nested_frame_ticks += elapsed;
    }
    if (g_depth > 0) {
        g_shadow[g_depth - 1].child_ticks += static_cast<int64_t>(elapsed);
    }
    return f.ret;
}

// --- thunks (x86 only) ---------------------------------------------------------------------------
// Entry: stack = [site][ret][args...], registers untouched. Exit: stack = [return_stub][args...],
// registers untouched, EIP = original target. The stub later restores the original return address.

extern "C" void __declspec(naked) pp_profiler_return_stub() {
    __asm {
        sub esp, 4                  ; slot for the original return address
        push eax
        push ecx
        push edx
        pushfd
        sub esp, 108                ; x87 state
        fnsave [esp]
        sub esp, 128
        movdqu [esp], xmm0
        movdqu [esp + 16], xmm1
        movdqu [esp + 32], xmm2
        movdqu [esp + 48], xmm3
        movdqu [esp + 64], xmm4
        movdqu [esp + 80], xmm5
        movdqu [esp + 96], xmm6
        movdqu [esp + 112], xmm7
        lea eax, [esp + 256]        ; ESP at stub entry
        push eax
        call pp_profiler_leave
        add esp, 4
        mov [esp + 128 + 108 + 16], eax   ; slot := original return address
        movdqu xmm0, [esp]
        movdqu xmm1, [esp + 16]
        movdqu xmm2, [esp + 32]
        movdqu xmm3, [esp + 48]
        movdqu xmm4, [esp + 64]
        movdqu xmm5, [esp + 80]
        movdqu xmm6, [esp + 96]
        movdqu xmm7, [esp + 112]
        add esp, 128
        frstor [esp]
        add esp, 108
        popfd
        pop edx
        pop ecx
        pop eax
        ret                         ; pops the slot: no register is touched on the way out
    }
}

extern "C" void __declspec(naked) pp_profiler_thunk_common() {
    __asm {
        pushad                      ; +32: [regs][site][ret]
        pushfd                      ; +4
        sub esp, 108                ; x87 state
        fnsave [esp]
        sub esp, 128                ; xmm save area
        movdqu [esp], xmm0
        movdqu [esp + 16], xmm1
        movdqu [esp + 32], xmm2
        movdqu [esp + 48], xmm3
        movdqu [esp + 64], xmm4
        movdqu [esp + 80], xmm5
        movdqu [esp + 96], xmm6
        movdqu [esp + 112], xmm7
        ; site at [esp+272], ret at [esp+276]  (128 xmm + 108 x87 + 4 flags + 32 regs)
        lea eax, [esp + 276]        ; address of the return-address slot (= ESP at callee entry)
        push eax
        push dword ptr [esp + 280]  ; ret (shifted by the push above)
        push dword ptr [esp + 280]  ; site (shifted by two pushes)
        call pp_profiler_enter    ; eax = original target
        add esp, 12
        mov [esp + 272], eax        ; site slot := target
        mov dword ptr [esp + 276], offset pp_profiler_return_stub
        movdqu xmm0, [esp]
        movdqu xmm1, [esp + 16]
        movdqu xmm2, [esp + 32]
        movdqu xmm3, [esp + 48]
        movdqu xmm4, [esp + 64]
        movdqu xmm5, [esp + 80]
        movdqu xmm6, [esp + 96]
        movdqu xmm7, [esp + 112]
        add esp, 128
        frstor [esp]
        add esp, 108
        popfd
        popad
        ret                         ; pops the target and jumps to it; stack is now [stub][args...]
    }
}

#define PP_THUNK(N)                                             \
    extern "C" void __declspec(naked) pp_profiler_thunk_##N() { \
        __asm { push N }                                          \
        __asm { jmp pp_profiler_thunk_common }                  \
    }
// Thunk N must push N and sit at kThunks[N]: ids are decimal, generated ten at a time.
#define PP_THUNK10(B) \
    PP_THUNK(B##0) PP_THUNK(B##1) PP_THUNK(B##2) PP_THUNK(B##3) PP_THUNK(B##4) \
    PP_THUNK(B##5) PP_THUNK(B##6) PP_THUNK(B##7) PP_THUNK(B##8) PP_THUNK(B##9)
PP_THUNK(0) PP_THUNK(1) PP_THUNK(2) PP_THUNK(3) PP_THUNK(4)
PP_THUNK(5) PP_THUNK(6) PP_THUNK(7) PP_THUNK(8) PP_THUNK(9)
PP_THUNK10(1) PP_THUNK10(2) PP_THUNK10(3) PP_THUNK10(4) PP_THUNK10(5)
PP_THUNK10(6) PP_THUNK10(7) PP_THUNK10(8) PP_THUNK10(9)
PP_THUNK10(10) PP_THUNK10(11) PP_THUNK10(12) PP_THUNK10(13) PP_THUNK10(14)
PP_THUNK10(15) PP_THUNK10(16) PP_THUNK10(17) PP_THUNK10(18) PP_THUNK10(19)
PP_THUNK10(20) PP_THUNK10(21) PP_THUNK10(22) PP_THUNK10(23) PP_THUNK10(24)
PP_THUNK10(25) PP_THUNK10(26) PP_THUNK10(27) PP_THUNK10(28) PP_THUNK10(29)
PP_THUNK10(30) PP_THUNK10(31) PP_THUNK10(32) PP_THUNK10(33) PP_THUNK10(34)
PP_THUNK10(35) PP_THUNK10(36) PP_THUNK10(37) PP_THUNK10(38) PP_THUNK10(39)
PP_THUNK10(40) PP_THUNK10(41) PP_THUNK10(42) PP_THUNK10(43) PP_THUNK10(44)
PP_THUNK10(45) PP_THUNK10(46) PP_THUNK10(47) PP_THUNK10(48) PP_THUNK10(49)
PP_THUNK10(50) PP_THUNK10(51) PP_THUNK10(52) PP_THUNK10(53) PP_THUNK10(54)
PP_THUNK10(55) PP_THUNK10(56) PP_THUNK10(57) PP_THUNK10(58) PP_THUNK10(59)
#undef PP_THUNK10
#undef PP_THUNK

namespace {
#define T(N) pp_profiler_thunk_##N
#define T10(B) T(B##0), T(B##1), T(B##2), T(B##3), T(B##4), T(B##5), T(B##6), T(B##7), T(B##8), T(B##9)
void (*const kThunks[kMaxSites])() = {
    T(0), T(1), T(2), T(3), T(4), T(5), T(6), T(7), T(8), T(9),
    T10(1), T10(2), T10(3), T10(4), T10(5), T10(6), T10(7), T10(8), T10(9),
    T10(10), T10(11), T10(12), T10(13), T10(14), T10(15), T10(16), T10(17), T10(18), T10(19),
    T10(20), T10(21), T10(22), T10(23), T10(24), T10(25), T10(26), T10(27), T10(28), T10(29),
    T10(30), T10(31), T10(32), T10(33), T10(34), T10(35), T10(36), T10(37), T10(38), T10(39),
    T10(40), T10(41), T10(42), T10(43), T10(44), T10(45), T10(46), T10(47), T10(48), T10(49),
    T10(50), T10(51), T10(52), T10(53), T10(54), T10(55), T10(56), T10(57), T10(58), T10(59),
};
#undef T10
#undef T
constexpr int kThunkCount = kMaxSites;
static_assert(kMaxSites == 600, "thunk table covers ids 0..599");

int target_index(uint32_t rva) {
    for (size_t i = 0; i < g_targets.size(); i++) {
        if (g_targets[i].rva == rva) {
            return static_cast<int>(i);
        }
    }
    Target t{};
    t.rva = rva;
    const char* name = nullptr;
    for (const Known& k : kKnown) {
        if (k.target_rva == rva) {
            name = k.name;
        }
    }
    if (name) {
        snprintf(t.name, sizeof(t.name), "%s", name);
    } else {
        snprintf(t.name, sizeof(t.name), "f_%08X", rva + 0x400000);
    }
    g_targets.push_back(t);
    return static_cast<int>(g_targets.size() - 1);
}

bool add_site(uint32_t site_rva, uint32_t target_rva) {
    if (g_site_count >= kThunkCount) {
        return false;
    }
    uintptr_t site = cp::hooks::exe_addr(site_rva);
    uintptr_t target = cp::hooks::exe_addr(target_rva);
    unsigned char expected[5] = {0xE8};
    int32_t rel = static_cast<int32_t>(target - (site + 5));
    *reinterpret_cast<int32_t*>(expected + 1) = rel;
    if (!cp::hooks::expect_bytes(site, expected, 5)) {
        return false;
    }
    g_site_target_addr[g_site_count] = target;
    g_site_target_index[g_site_count] = target_index(target_rva);
    g_sites_added.push_back({site_rva, target_rva, false});
    g_site_count++;
    return true;
}

// A vtable slot (or any stored function pointer) that currently holds `target_rva`.
bool add_slot(uint32_t slot_rva, uint32_t target_rva) {
    if (g_site_count >= kThunkCount) {
        return false;
    }
    uintptr_t target = cp::hooks::exe_addr(target_rva);
    if (*reinterpret_cast<const uintptr_t*>(cp::hooks::exe_addr(slot_rva)) != target) {
        return false;
    }
    g_site_target_addr[g_site_count] = target;
    g_site_target_index[g_site_count] = target_index(target_rva);
    g_sites_added.push_back({slot_rva, target_rva, true});
    g_site_count++;
    return true;
}
}  // namespace

namespace cp::features {

void install_tick_profiler(const Config& config) {
    if (config.tick_profiler <= 0) {
        logf("tick_profiler: disabled");
        return;
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;

    // Level 1 must match completely or nothing is patched: a partial set would skew the exclusive times.
    for (const Level1& b : kLevel1) {
        if (!add_site(b.site_rva, b.target_rva)) {
            logf("tick_profiler: call site %08X not recognised, feature off", b.site_rva + 0x400000);
            g_site_count = 0;
            g_targets.clear();
            return;
        }
    }
    // The per-frame function is reached through its vtable (CInGameIdler slot at 0x00E0A350).
    if (!add_slot(0xA0A350, 0x254D80)) {
        logf("tick_profiler: CInGameIdler::Update slot not recognised, feature off");
        g_site_count = 0;
        g_targets.clear();
        return;
    }
    int level1 = g_site_count;
    int skipped = 0;
    if (config.tick_profiler >= 2) {
        for (const tick_sites::Site& s : tick_sites::kDayTickCalls) {
            if (!add_site(s.site_rva, s.target_rva)) {
                skipped++;
            }
        }
    }
    if (config.tick_profiler >= 3) {
        for (const tick_sites::Site& s : tick_sites::kLevel3Calls) {
            if (!add_site(s.site_rva, s.target_rva)) {
                skipped++;
            }
        }
    }
    if (config.tick_profiler >= 4) {
        for (const tick_sites::Site& s : tick_sites::kFrameCalls) {
            if (!add_site(s.site_rva, s.target_rva)) {
                skipped++;
            }
        }
    }
    if (config.tick_profiler >= 5) {
        for (const tick_sites::Site& s : tick_sites::kLevel5Calls) {
            if (!add_site(s.site_rva, s.target_rva)) {
                skipped++;
            }
        }
    }
    g_world_target = target_index(0x282EC0);
    g_frame_target = target_index(0x254D80);

    // Each redirect is independent; ids follow the order the sites were added.
    for (int site = 0; site < g_site_count; site++) {
        const Added& a = g_sites_added[site];
        if (a.slot) {
            hooks::replace_pointer(hooks::exe_addr(a.rva), g_site_target_addr[site],
                                   reinterpret_cast<uintptr_t>(kThunks[site]));
        } else {
            hooks::redirect_call(hooks::exe_addr(a.rva), g_site_target_addr[site],
                                 reinterpret_cast<uintptr_t>(kThunks[site]));
        }
    }
    logf("tick_profiler: active at level %d, %d call sites (%d level-1, %d skipped), %zu targets, report every %d game days",
         config.tick_profiler, g_site_count, level1, skipped, g_targets.size(), kReportEveryDays);
}

}  // namespace cp::features

namespace cp::features {

void tick_profiler_crash_context() {
    if (g_site_count == 0) {
        return;
    }
    logf("crash:   tick_profiler depth %d", g_depth);
    for (int i = 0; i < g_depth && i < kMaxDepth; i++) {
        const Frame& f = g_shadow[i];
        if (f.target >= 0 && static_cast<size_t>(f.target) < g_targets.size()) {
            logf("crash:   active[%d] %s (%08X), returns to %08X", i, g_targets[f.target].name,
                 g_targets[f.target].rva + 0x400000, static_cast<unsigned>(f.ret));
        }
    }
}

}  // namespace cp::features
