#include "features/cb_cull.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "features/cb_cull_triggers.h"
#include "hooks/memory.h"
#include "log.h"

// Background (re/systems/casus-belli.md, docs/cb-cull.md):
//
//   AI_DailyUpdate -> AI_ScoreWarTargets (0x0085DB40) / AI_ScoreDiplomaticTargets (0x0085E0E0)
//     for every other country D:  CBManager_IsAnyCBUsable(D->rel[self], &D, &self)      0x0092EDE0
//       for every CB type T (manager order):
//         if (!T->IsRealType()) continue;                                            vtable slot 7
//         IsCBUsable(rel, &D, &self; EDI = T.index):                                  0x0092EEC0
//           walk rel+0x50 (CBs self holds against D) for one of type T;
//           if none and !T.always (+0x132) -> false;
//           else build two CEventScopes (self; THIS = D) and return T->CanUse(scope)  vtable slot 8
//       return true at the first usable type
//
// So per (attacker, defender) pair the original walks the held list once per type (~100 times) and
// builds two scopes per `always` type. The result is a boolean OR, so order does not matter. This
// replacement, installed at the two AI call sites, computes per (attacker, day) which `always` types
// survive their attacker-only conditions (a direct child of `can_use` that never looks at THIS/FROM is
// the same for every defender), then per pair builds the two scopes once and calls CanUse only for
// those types and for the types actually held against that defender (one walk of the held list).

namespace cp::features {

namespace {

// --- v2game.exe 3.04 (RVA = VA - 0x00400000) ---------------------------------------------------
constexpr uint32_t kIsAnyCBUsableRva = 0x52EDE0;    // CBManager_IsAnyCBUsable(rel, defRef, attRef), __stdcall
constexpr uint32_t kScopeCtorRva = 0x4A8650;        // CEventScope::ctor(this, parent = 0)
constexpr uint32_t kScanCallSites[] = {0x45DCDA,    // in AI_ScoreWarTargets
                                       0x45E5FB};   // in AI_ScoreDiplomaticTargets
constexpr uint32_t kWorldPtrRva = 0xE588E8;         // DAT_012588E8
constexpr uint32_t kWorldDateOffset = 0xB0C;        // hours
constexpr uint32_t kCountryManagerPtrRva = 0xE587E4;  // DAT_012587E4, +0 = count
constexpr uint32_t kCBManagerPtrRva = 0xF1ACAC;     // DAT_0131ACAC, +0xC/+0x10 = vector<CCasusBelliType*>
constexpr uint32_t kAndVtableRva = 0x9F9994;        // CAndTrigger::vftable
constexpr uint32_t kOrVtableRva = 0xA2FB70;         // COrTrigger::vftable
constexpr uint32_t kNotVtableRva = 0xA2FBA8;        // CNotTrigger::vftable

constexpr uint32_t kCBTypeCanUse = 0x4C;     // CCasusBelliType::can_use (embedded CAndTrigger)
constexpr uint32_t kCBTypeName = 0x14;       // std::string (SSO): buffer/pointer +0x14, size +0x24, capacity +0x28
constexpr uint32_t kCBTypeAlways = 0x132;    // `always = yes`
constexpr uint32_t kVtableIsRealType = 0x1C; // CCasusBelliType slot 7
constexpr uint32_t kVtableCanUse = 0x20;     // CCasusBelliType slot 8: bool CanUse(scope) = can_use.Evaluate
constexpr uint32_t kRelationHeldList = 0x50; // relation object: singly linked list of held CCasusBelli
constexpr uint32_t kHeldNodeCB = 0x00;       //   node: +0 CCasusBelli*, +8 next
constexpr uint32_t kHeldNodeNext = 0x08;
constexpr uint32_t kCBTypeIndex = 0x24;      // CCasusBelli: index of its type in the manager list
constexpr uint32_t kBlockBegin = 0x08;       // trigger block: vector<CTrigger*> begin
constexpr uint32_t kBlockEnd = 0x0C;         //                                 end
constexpr uint32_t kTriggerThisFlag = 0x18;  // value was `THIS`
constexpr uint32_t kTriggerFromFlag = 0x19;  // value was `FROM`
constexpr uint32_t kVtableEvaluate = 0x18;   // CTrigger slot 6: bool Evaluate(scope)
constexpr uint32_t kScopeTag = 0x08;         // CEventScope: country tag
constexpr uint32_t kScopeIndex = 0x0C;       //              country index
constexpr uint32_t kScopeThis = 0x24;        //              THIS scope pointer
constexpr size_t kScopeSize = 0x60;          // ctor writes up to +0x48

constexpr int32_t kReportEveryDays = 30;
constexpr uint64_t kSample = 64;  // timing sample rate (power of two)

// 3 stack arguments, callee cleans (`RET 0xC`), no register arguments.
using IsAnyCBUsableFn = char(__stdcall*)(void* relation, void* defender_ref, void* attacker_ref);
// __thiscall in the game; __fastcall takes `this` in ecx and ignores edx.
using EvaluateFn = char(__fastcall*)(void* self, void* edx, void* scope);
using IsRealTypeFn = char(__fastcall*)(void* self, void* edx);
using ScopeCtorFn = void*(__fastcall*)(void* self, void* edx, void* parent);

IsAnyCBUsableFn g_is_any_cb_usable = nullptr;
ScopeCtorFn g_scope_ctor = nullptr;
bool g_profiling = false;

uintptr_t g_and_vtable = 0;
uintptr_t g_or_vtable = 0;
uintptr_t g_not_vtable = 0;
std::unordered_set<uintptr_t> g_attacker_only_classes;

// Per CB type, static once the files are loaded.
struct TypeInfo {
    uintptr_t type = 0;
    bool real = false;    // IsRealType(): CNullCasusBelliType is skipped by the original loop
    bool always = false;  // `always = yes`: usable without holding it
    std::vector<uintptr_t> attacker_only;  // direct children of can_use that never look at THIS/FROM
};
std::vector<TypeInfo> g_types;

// Per attacker: the game day the pre-pass ran, which types it killed, and the live `always` types.
struct AttackerState {
    int32_t day = -1;
    std::vector<uint8_t> dead;           // per type index: an attacker-only condition failed today
    std::vector<uint32_t> live_always;   // real `always` types not dead today
};
std::vector<AttackerState> g_attackers;

struct Stats {
    uint64_t scans = 0;          // IsAnyCBUsable calls from the AI
    uint64_t can_use_always = 0; // CanUse calls for live `always` types
    uint64_t can_use_held = 0;   // CanUse calls for held, non-`always` types
    uint64_t hits = 0;           // scans that returned true
    uint64_t scan_ticks = 0;     // sampled
    uint64_t prepass = 0;        // (attacker, day) pre-passes
    uint64_t prepass_ticks = 0;
    std::vector<uint64_t> can_use_per_type;
};
Stats g_stats;
int32_t g_stats_day = -1;
int32_t g_window_start_day = -1;
int64_t g_window_start_qpc = 0;
int64_t g_qpc_freq = 0;

inline uint32_t read32(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(at); }
inline uint8_t read8(uintptr_t at) { return *reinterpret_cast<const uint8_t*>(at); }

inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
inline double ms(uint64_t ticks) {
    return g_qpc_freq ? 1000.0 * static_cast<double>(ticks) / static_cast<double>(g_qpc_freq) : 0.0;
}

int32_t current_day() {
    uintptr_t world = read32(hooks::exe_addr(kWorldPtrRva));
    return world ? static_cast<int32_t>(read32(world + kWorldDateOffset)) / 24 : -1;
}

size_t country_count() {
    uintptr_t manager = read32(hooks::exe_addr(kCountryManagerPtrRva));
    return manager ? read32(manager) : 0;
}

uintptr_t cb_types_begin() {
    uintptr_t manager = read32(hooks::exe_addr(kCBManagerPtrRva));
    return manager ? read32(manager + 0x0C) : 0;
}

size_t cb_type_count() {
    uintptr_t manager = read32(hooks::exe_addr(kCBManagerPtrRva));
    return manager ? (read32(manager + 0x10) - read32(manager + 0x0C)) / sizeof(uint32_t) : 0;
}

std::string cb_type_name(uintptr_t type) {
    uint32_t size = read32(type + kCBTypeName + 0x10);
    uint32_t capacity = read32(type + kCBTypeName + 0x14);
    const char* data = capacity > 15 ? reinterpret_cast<const char*>(read32(type + kCBTypeName))
                                     : reinterpret_cast<const char*>(type + kCBTypeName);
    return std::string(data, size > 64 ? 64 : size);
}

// --- trigger classification ----------------------------------------------------------------------

bool is_attacker_only(uintptr_t trigger);

bool children_attacker_only(uintptr_t block) {
    for (uintptr_t it = read32(block + kBlockBegin), end = read32(block + kBlockEnd); it < end; it += 4) {
        if (!is_attacker_only(read32(it))) {
            return false;
        }
    }
    return true;
}

bool is_attacker_only(uintptr_t trigger) {
    uintptr_t vtable = read32(trigger);
    if (vtable == g_and_vtable || vtable == g_or_vtable || vtable == g_not_vtable) {
        return children_attacker_only(trigger);
    }
    if (!g_attacker_only_classes.contains(vtable)) {
        return false;
    }
    return read8(trigger + kTriggerThisFlag) == 0 && read8(trigger + kTriggerFromFlag) == 0;
}

inline bool evaluate(uintptr_t trigger, void* scope) {
    auto fn = reinterpret_cast<EvaluateFn>(read32(read32(trigger) + kVtableEvaluate));
    return fn(reinterpret_cast<void*>(trigger), nullptr, scope) != 0;
}

inline bool is_real_type(uintptr_t type) {
    auto fn = reinterpret_cast<IsRealTypeFn>(read32(read32(type) + kVtableIsRealType));
    return fn(reinterpret_cast<void*>(type), nullptr) != 0;
}

inline bool can_use(uintptr_t type, void* scope) {
    auto fn = reinterpret_cast<EvaluateFn>(read32(read32(type) + kVtableCanUse));
    return fn(reinterpret_cast<void*>(type), nullptr, scope) != 0;
}

// Rebuilds g_types when the CB list changed (first use, or a reload).
void refresh_types() {
    size_t count = cb_type_count();
    uintptr_t begin = cb_types_begin();
    bool same = g_types.size() == count;
    for (size_t i = 0; same && i < count; i++) {
        same = g_types[i].type == read32(begin + i * 4);
    }
    if (same) {
        return;
    }
    g_types.assign(count, TypeInfo{});
    size_t classified = 0, always = 0;
    for (size_t i = 0; i < count; i++) {
        TypeInfo& info = g_types[i];
        info.type = read32(begin + i * 4);
        info.real = is_real_type(info.type);
        info.always = read8(info.type + kCBTypeAlways) != 0;
        uintptr_t block = info.type + kCBTypeCanUse;
        for (uintptr_t it = read32(block + kBlockBegin), end = read32(block + kBlockEnd); it < end; it += 4) {
            uintptr_t child = read32(it);
            if (is_attacker_only(child)) {
                info.attacker_only.push_back(child);
            }
        }
        classified += info.attacker_only.empty() ? 0 : 1;
        always += info.always && info.real ? 1 : 0;
    }
    for (AttackerState& a : g_attackers) {
        a.day = -1;
    }
    logf("cb_cull: %zu CB types, %zu `always`, %zu with attacker-only conditions in can_use", count, always, classified);
}

// --- scopes --------------------------------------------------------------------------------------

// Exactly what IsCBUsable builds before CanUse: scope = attacker, scope->THIS = defender, no FROM.
struct ScopePair {
    alignas(16) unsigned char main[kScopeSize];
    alignas(16) unsigned char this_scope[kScopeSize];
};

void build_scopes(ScopePair& s, void* attacker_ref, void* defender_ref) {
    std::memset(&s, 0, sizeof(s));
    g_scope_ctor(s.main, nullptr, nullptr);
    g_scope_ctor(s.this_scope, nullptr, nullptr);
    std::memcpy(s.main + kScopeTag, attacker_ref, 8);        // {tag, index}
    std::memcpy(s.this_scope + kScopeTag, defender_ref, 8);
    *reinterpret_cast<unsigned char**>(s.main + kScopeThis) = s.this_scope;
}

// --- the per-attacker pre-pass -------------------------------------------------------------------

// Evaluates the attacker-only conditions of every type once per (attacker, day).
AttackerState* state_for(void* attacker_ref, int32_t day) {
    uint32_t attacker = read32(reinterpret_cast<uintptr_t>(attacker_ref) + 4);
    if (attacker >= g_attackers.size()) {
        size_t countries = country_count();
        if (attacker >= countries) {
            return nullptr;
        }
        g_attackers.resize(countries);
    }
    AttackerState& state = g_attackers[attacker];
    if (state.day == day) {
        return &state;
    }

    int64_t start = g_profiling ? qpc() : 0;
    alignas(16) unsigned char scope[kScopeSize];
    std::memset(scope, 0, sizeof(scope));
    g_scope_ctor(scope, nullptr, nullptr);
    std::memcpy(scope + kScopeTag, attacker_ref, 8);

    state.dead.assign(g_types.size(), 0);
    state.live_always.clear();
    for (uint32_t i = 0; i < g_types.size(); i++) {
        const TypeInfo& info = g_types[i];
        if (!info.real) {
            state.dead[i] = 1;  // the original loop skips these too
            continue;
        }
        for (uintptr_t child : info.attacker_only) {
            if (!evaluate(child, scope)) {
                state.dead[i] = 1;
                break;
            }
        }
        if (!state.dead[i] && info.always) {
            state.live_always.push_back(i);
        }
    }
    state.day = day;
    if (g_profiling) {
        g_stats.prepass++;
        g_stats.prepass_ticks += static_cast<uint64_t>(qpc() - start);
    }
    return &state;
}

// --- statistics ----------------------------------------------------------------------------------

void report_window(int32_t last_day) {
    int32_t days = last_day - g_window_start_day + 1;
    if (days <= 0 || g_stats.scans == 0) {
        return;
    }
    double wall_ms = ms(static_cast<uint64_t>(qpc() - g_window_start_qpc));
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    logf("cb_cull: days %d-%d (%d days, %.1f ms/day wall): %llu AI CB scans/day in %.2f ms/day (%llu hits/day); "
         "can_use calls/day: %llu for live `always` types, %llu for held types; pre-pass %.2f ms/day over %llu attacker-days",
         g_window_start_day, last_day, days, wall_ms / days, ull(g_stats.scans / days), ms(g_stats.scan_ticks) / days,
         ull(g_stats.hits / days), ull(g_stats.can_use_always / days), ull(g_stats.can_use_held / days),
         ms(g_stats.prepass_ticks) / days, ull(g_stats.prepass));

    std::vector<size_t> order;
    for (size_t i = 0; i < g_stats.can_use_per_type.size() && i < g_types.size(); i++) {
        if (g_stats.can_use_per_type[i]) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(),
              [](size_t a, size_t b) { return g_stats.can_use_per_type[a] > g_stats.can_use_per_type[b]; });
    for (size_t n = 0; n < order.size() && n < 8; n++) {
        logf("cb_cull:   %-28s can_use %llu calls/day", cb_type_name(g_types[order[n]].type).c_str(),
             ull(g_stats.can_use_per_type[order[n]] / days));
    }
}

void roll_stats(int32_t day) {
    if (g_stats_day == day) {
        return;
    }
    if (g_window_start_day < 0) {
        g_window_start_day = day;
        g_window_start_qpc = qpc();
    } else if (day - g_window_start_day >= kReportEveryDays) {
        report_window(g_stats_day);
        g_stats = Stats{};
        g_window_start_day = day;
        g_window_start_qpc = qpc();
    }
    g_stats_day = day;
}

// --- the hook ------------------------------------------------------------------------------------

char __stdcall is_any_cb_usable_hook(void* relation, void* defender_ref, void* attacker_ref) {
    int32_t day = current_day();
    if (day < 0) {
        return g_is_any_cb_usable(relation, defender_ref, attacker_ref);
    }
    refresh_types();
    AttackerState* state = state_for(attacker_ref, day);
    if (!state) {
        return g_is_any_cb_usable(relation, defender_ref, attacker_ref);
    }

    bool sampled = false;
    int64_t start = 0;
    if (g_profiling) {
        roll_stats(day);
        sampled = (g_stats.scans & (kSample - 1)) == 0;
        start = sampled ? qpc() : 0;
        g_stats.scans++;
        if (g_stats.can_use_per_type.size() < g_types.size()) {
            g_stats.can_use_per_type.resize(g_types.size());
        }
    }

    ScopePair scopes;
    build_scopes(scopes, attacker_ref, defender_ref);
    char result = 0;

    // `always = yes` types that survived the attacker-only conditions today
    for (uint32_t i : state->live_always) {
        if (g_profiling) {
            g_stats.can_use_always++;
            g_stats.can_use_per_type[i]++;
        }
        if (can_use(g_types[i].type, scopes.main)) {
            result = 1;
            break;
        }
    }
    // CBs the attacker actually holds against this defender (the original finds these by walking
    // the same list once per type); `always` types were covered above, dead ones cannot pass
    if (!result && relation) {
        for (uintptr_t node = read32(reinterpret_cast<uintptr_t>(relation) + kRelationHeldList); node;
             node = read32(node + kHeldNodeNext)) {
            uintptr_t cb = read32(node + kHeldNodeCB);
            if (!cb) {
                continue;
            }
            uint32_t i = read32(cb + kCBTypeIndex);
            if (i >= g_types.size() || g_types[i].always || state->dead[i]) {
                continue;
            }
            if (g_profiling) {
                g_stats.can_use_held++;
                g_stats.can_use_per_type[i]++;
            }
            if (can_use(g_types[i].type, scopes.main)) {
                result = 1;
                break;
            }
        }
    }

    if (g_profiling) {
        g_stats.hits += result ? 1 : 0;
        if (sampled) {
            g_stats.scan_ticks += static_cast<uint64_t>(qpc() - start) * kSample;
        }
    }
    return result;
}

}  // namespace

void install_cb_cull(const Config& config) {
    if (!config.cull_cb_checks) {
        logf("cb_cull: disabled");
        return;
    }
    g_profiling = config.cull_cb_profiling;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;

    g_and_vtable = hooks::exe_addr(kAndVtableRva);
    g_or_vtable = hooks::exe_addr(kOrVtableRva);
    g_not_vtable = hooks::exe_addr(kNotVtableRva);
    for (const auto& t : cb_cull_triggers::kCountryRef) {
        g_attacker_only_classes.insert(hooks::exe_addr(t.vtable_rva));
    }
    for (const auto& t : cb_cull_triggers::kScalar) {
        g_attacker_only_classes.insert(hooks::exe_addr(t.vtable_rva));
    }
    g_scope_ctor = reinterpret_cast<ScopeCtorFn>(hooks::exe_addr(kScopeCtorRva));

    uintptr_t original_scan = hooks::exe_addr(kIsAnyCBUsableRva);
    if (!hooks::expect_bytes(reinterpret_cast<uintptr_t>(g_scope_ctor), "\x55\x8B\xEC", 3) ||
        !hooks::expect_bytes(original_scan, "\x55\x8B\xEC\x6A\xFF", 5)) {
        logf("cb_cull: IsAnyCBUsable / CEventScope ctor not recognised, feature off");
        return;
    }
    for (uint32_t site : kScanCallSites) {
        if (!hooks::expect_bytes(hooks::exe_addr(site), "\xE8", 1)) {
            logf("cb_cull: AI scan call site %08X not recognised, feature off", site + 0x400000);
            return;
        }
    }
    g_is_any_cb_usable = reinterpret_cast<IsAnyCBUsableFn>(original_scan);
    uintptr_t wrapper = reinterpret_cast<uintptr_t>(&is_any_cb_usable_hook);
    for (uint32_t site : kScanCallSites) {
        if (!hooks::redirect_call(hooks::exe_addr(site), original_scan, wrapper)) {
            logf("cb_cull: AI scan call site %08X does not call IsAnyCBUsable, feature off", site + 0x400000);
            return;  // an already redirected site still behaves correctly
        }
    }
    logf("cb_cull: active (v3: one scope pair per target, can_use only for live `always` and held types; %zu attacker-only "
         "trigger classes), profiling %s",
         g_attacker_only_classes.size(), g_profiling ? "on" : "off");
}

}  // namespace cp::features
