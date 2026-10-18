#include "features/performance/decision_cull.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "features/performance/decision_cull_internal.h"
#include "game/string.h"
#include "game/triggers.h"
#include "game/world.h"
#include "hooks/hourly_tick.h"
#include "hooks/memory.h"
#include "log.h"
#include "util/clock.h"

// Background (docs/re/systems/decisions.md, docs/cull-decision-checks.md, docs/re/raw/decision-keyword-map.txt):
//
//   AI_DecisionUpdate (0x00821190), once per AI country per day, driven from the per-frame AI job
//   (CInGameIdler::Update -> FUN_006874C0 -> FUN_0081E9E0 -> CALL at 0x0081EA9B, ECX = AI object):
//     reads DAT_01319730 directly; for every decision d in both lists:
//       if (d->vt[6]() && d->potential.Evaluate(scope) && d->allow.Evaluate(scope)) { ai_will_do; roll; execute }
//   This is where the ~529 k potential evaluations a day come from (1651 decisions x ~320 countries).
//   AI_DiplomacyUpdate (0x0085D270) has the same two loops through DecisionManager_Get() (CALL
//   0x00875040 at 0x0085D34F / 0x0085D459, ESI = AI object, +0x0C index) but runs for a few countries
//   a day only. The player's alert (FUN_005399C0) and the UI read the global too; they are left alone.
//
// cull_decision_checks gives those AI loops a private copy of the manager whose two lists hold, for
// that country, only the decisions that can pass: a top-level `tag = X` must be X, a top-level
// `ai = yes/no` must hold, and a top-level `NOT { has_*_flag }` gate must be true. For
// AI_DecisionUpdate the global is pointed at the copy for the duration of the call; for
// AI_DiplomacyUpdate the two getter calls are redirected. A country's lists are built once and
// rebuilt when something can bring a decision back (a flag cleared for that country or globally, a
// load) or every kRefreshDays to drop the ones that died since. The loops themselves are untouched and
// the AI pays nothing per decision it no longer sees.
//
// parallel_decisions (decision_parallel.cpp) evaluates those lists on worker threads ahead of the AI;
// decision_profiling (decision_profiling.cpp) times every decision's `potential`.

namespace cp::features::decisions {

// --- shared state (decision_cull_internal.h) -------------------------------------------------------
bool g_cull = false;
bool g_profiling = false;
game::ScopeCtorFn g_scope_ctor = nullptr;
std::vector<DecisionInfo> g_decisions;
std::vector<CountryLists> g_lists;
CullStats g_cull_stats;
ReportWindow g_window;

namespace {

// --- v2game.exe 3.04 (RVA = VA - 0x00400000) ---------------------------------------------------
constexpr uint32_t kDecisionManagerPtrRva = 0xF19730;    // DAT_01319730; lists at +0xC/+0x10 and +0x28/+0x2C
constexpr uint32_t kDecisionManagerGetterRva = 0x475040; // DecisionManager_Get()
constexpr uint32_t kGetterCallSites[] = {0x45D34F, 0x45D459};  // in AI_DiplomacyUpdate
constexpr uint32_t kAiDecisionsRva = 0x421190;  // AI_DecisionUpdate(ai): the daily per-country loop (frame-driven)
constexpr uint32_t kAiDecisionsCallSiteRva = 0x41EA9B;   // in FUN_0081E9E0, `MOV ECX,ESI; CALL`; ESI = AI object
constexpr uint32_t kTagVtableRva = 0xA30C80;             // CTagTrigger::vftable (+0x1C tag, +0x20 country index)
constexpr uint32_t kHasCountryFlagVtableRva = 0xA307E8;  // CHasCountryFlagTrigger::vftable
constexpr uint32_t kHasGlobalFlagVtableRva = 0xA30820;   // CHasGlobalFlagTrigger::vftable
constexpr uint32_t kIsAiVtableRva = 0xA30EE8;            // CIsAiTrigger::vftable (`ai = yes/no`, value at +0x1C)
constexpr uint32_t kClrCountryFlagSlotRva = 0xA2ACC0 + 0x2C;  // CClrCountryFlagEffect::vftable slot 11 (Execute)
constexpr uint32_t kClrCountryFlagExecuteRva = 0x482A40;
constexpr uint32_t kClrGlobalFlagSlotRva = 0xA2ADA0 + 0x2C;   // CClrGlobalFlagEffect::vftable slot 11 (Execute)
constexpr uint32_t kClrGlobalFlagExecuteRva = 0x482BA0;
constexpr uint32_t kTagTriggerIndex = 0x20;  // CTagTrigger: country index of the literal tag
constexpr size_t kNameMax = 64;              // characters of a decision name kept for logs

constexpr int32_t kRefreshDays = 90;  // a country's lists are rebuilt this often to drop newly dead pairs

// CEffect::Execute(scope, arg2) is __thiscall with two stack arguments (`RET 0x8`); the second one is passed through.
using ExecuteFn = void(__fastcall*)(void* self, void* edx, void* scope, void* arg2);
using AiDecisionsFn = int(__fastcall*)(void* ai, void* edx);

ExecuteFn g_original_clr_country_flag = nullptr;
ExecuteFn g_original_clr_global_flag = nullptr;
AiDecisionsFn g_original_ai_decisions = nullptr;
uintptr_t g_and_vtable = 0;
uintptr_t g_not_vtable = 0;
uintptr_t g_tag_vtable = 0;
uintptr_t g_has_country_flag_vtable = 0;
uintptr_t g_has_global_flag_vtable = 0;
uintptr_t g_is_ai_vtable = 0;
alignas(16) uint8_t g_fake_manager[kManagerSize];

using game::read32;
using game::read8;

// The controller entry of a country (0 = AI); -1 when the table is shorter.
int32_t controller_of(size_t country) {
    uintptr_t world = game::world();
    if (!world) {
        return -1;
    }
    uintptr_t begin = read32(world + game::kWorldHumanPlayers);
    uintptr_t end = read32(world + game::kWorldHumanPlayersEnd);
    return country < (end - begin) / 4 ? static_cast<int32_t>(read32(begin + country * 4)) : -1;
}

uintptr_t decision_manager() { return read32(hooks::exe_addr(kDecisionManagerPtrRva)); }

bool is_plain_value(uintptr_t trigger) {
    return read8(trigger + game::kTriggerThisFlag) == 0 && read8(trigger + game::kTriggerFromFlag) == 0;
}

// --- static classification of one potential block -------------------------------------------------

bool block_is_only_flag_checks(uintptr_t block) {
    uintptr_t begin = read32(block + game::kTriggerBlockBegin), end = read32(block + game::kTriggerBlockEnd);
    if (begin == end) {
        return false;
    }
    for (uintptr_t it = begin; it < end; it += 4) {
        uintptr_t child = read32(it);
        uintptr_t vtable = read32(child);
        if (vtable != g_has_country_flag_vtable && vtable != g_has_global_flag_vtable) {
            return false;
        }
        if (!is_plain_value(child)) {
            return false;
        }
    }
    return true;
}

void classify_child(DecisionInfo& info, uintptr_t child) {
    uintptr_t vtable = read32(child);
    if (vtable == g_tag_vtable && info.locked_country < 0 && is_plain_value(child)) {
        info.locked_country = static_cast<int32_t>(read32(child + kTagTriggerIndex));
    } else if (vtable == g_not_vtable && info.gate == 0 && block_is_only_flag_checks(child)) {
        info.gate = child;
    } else if (vtable == g_is_ai_vtable && info.ai_child == 0 && read8(child + game::kTriggerThisFlag) == 0) {
        info.ai_child = child;
    }
}

void classify(DecisionInfo& info) {
    uintptr_t block = info.decision + kDecisionPotential;
    uintptr_t end = read32(block + game::kTriggerBlockEnd);
    for (uintptr_t it = read32(block + game::kTriggerBlockBegin); it < end; it += 4) {
        classify_child(info, read32(it));
    }
}

// --- the AI loops' manager -------------------------------------------------------------------------

// Called in place of DecisionManager_Get() from the AI loop; `country` is the AI's country index.
uintptr_t __cdecl manager_for_country(uint32_t country) {
    uintptr_t real = decision_manager();
    int32_t day = game::current_day();
    if (!real || day < 0 || g_decisions.empty() || country >= g_lists.size()) {
        return real;
    }
    CountryLists& lists = g_lists[country];
    if (lists_stale(lists, country, day)) {
        build_lists(country, day);
    }
    std::memcpy(g_fake_manager, reinterpret_cast<const void*>(real), kManagerSize);
    set_fake_list(kListA, lists.a);
    set_fake_list(kListB, lists.b);
    g_cull_stats.ai_requests++;
    g_cull_stats.ai_decisions_seen += lists.a.size() + lists.b.size();
    return reinterpret_cast<uintptr_t>(g_fake_manager);
}

// Called through a pointer so the compiler keeps the declared __cdecl convention for the asm call.
uintptr_t(__cdecl* volatile g_manager_for_country)(uint32_t) = &manager_for_country;

// The getter takes no arguments; the AI object is in ESI at both call sites (+0xC = country index,
// game::kAiDailyCountry).
__declspec(naked) void getter_stub() {
    __asm {
        push ecx
        push edx
        mov eax, dword ptr [esi + 0xC]
        push eax
        call dword ptr [g_manager_for_country]
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// AI_DecisionUpdate reads the manager through the global, so the global itself is pointed at the
// country's copy for the duration of the call and restored afterwards, also on an unwind.
int __fastcall ai_decisions_hook(void* ai, void* edx) {
    uintptr_t* global = reinterpret_cast<uintptr_t*>(hooks::exe_addr(kDecisionManagerPtrRva));
    uintptr_t real = *global;
    uint32_t country = read32(reinterpret_cast<uintptr_t>(ai) + game::kAiFrameJobCountry);
    uintptr_t replacement = manager_for_country(country);
    if (replacement == real) {
        return g_original_ai_decisions(ai, edx);
    }
    parallel_serve(country, reinterpret_cast<uintptr_t>(ai));
    int result = 0;
    *global = replacement;
    __try {
        result = g_original_ai_decisions(ai, edx);
    } __finally {
        *global = real;
    }
    return result;
}

void mark_dirty(size_t country) {
    if (country < g_lists.size()) {
        g_lists[country].dirty = true;
    }
}

void mark_all_dirty() {
    for (CountryLists& l : g_lists) {
        l.dirty = true;
    }
}

// --- effect hooks: a cleared flag can bring a decision back ---------------------------------------

inline uint32_t scope_country(void* scope) { return read32(reinterpret_cast<uintptr_t>(scope) + game::kScopeIndex); }

void __fastcall clr_country_flag_hook(void* self, void*, void* scope, void* arg2) {
    g_original_clr_country_flag(self, nullptr, scope, arg2);
    mark_dirty(scope_country(scope));  // rebuilt on the next AI update, never while a loop may be running
}

void __fastcall clr_global_flag_hook(void* self, void*, void* scope, void* arg2) {
    g_original_clr_global_flag(self, nullptr, scope, arg2);
    mark_all_dirty();
}

// --- reporting -----------------------------------------------------------------------------------

void log_cull_summary(int32_t last_day) {
    int32_t days = g_window.days_until(last_day);
    const CullStats& c = g_cull_stats;
    if (!g_cull || days <= 0 || c.ai_requests == 0) {
        return;
    }
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    auto per_build = [&c](uint64_t v) { return c.list_builds ? v / c.list_builds : 0; };
    double per_request = static_cast<double>(c.ai_decisions_seen) / static_cast<double>(c.ai_requests);
    logf("decision_cull: days %d-%d: AI loop saw %.0f of %zu decisions per country on average (%llu list "
         "requests/day); %llu list builds (%.2f ms/day) left out %llu by tag, %llu by ai, %llu dead per build",
         g_window.first_day, last_day, per_request, g_decisions.size(), ull(c.ai_requests / days),
         ull(c.list_builds), ms(c.list_build_ticks) / days, ull(per_build(c.removed_tag)),
         ull(per_build(c.removed_ai)), ull(per_build(c.removed_dead)));
}

void flush_reports(int32_t last_day) {
    write_report(last_day);
    log_cull_summary(last_day);
    log_parallel_summary(last_day);
}

// --- bookkeeping, once per game hour ------------------------------------------------------------

uintptr_t list_begin(uintptr_t manager, uint32_t list) { return read32(manager + list); }
uintptr_t list_end(uintptr_t manager, uint32_t list) { return read32(manager + list + 4); }

// True when g_decisions holds exactly the manager's two lists, in order.
bool decisions_unchanged(uintptr_t manager) {
    size_t count = 0;
    for (uint32_t list : {kListA, kListB}) {
        count += (list_end(manager, list) - list_begin(manager, list)) / 4;
    }
    if (g_decisions.size() != count) {
        return false;
    }
    size_t n = 0;
    for (uint32_t list : {kListA, kListB}) {
        for (uintptr_t it = list_begin(manager, list), end = list_end(manager, list); it < end; it += 4, n++) {
            if (g_decisions[n].decision != read32(it)) {
                return false;
            }
        }
    }
    return true;
}

DecisionInfo read_decision(uintptr_t decision, uint32_t list) {
    DecisionInfo info;
    info.decision = decision;
    info.list = list == kListA ? 0 : 1;
    uintptr_t vtable = read32(decision + kDecisionPotential);
    if (vtable == g_and_vtable || is_profiling_vtable(vtable)) {
        classify(info);
    }  // else an unexpected layout: kept in every list unclassified so the game still sees it
    return info;
}

void reset_statistics() {
    g_cull_stats = {};
    g_window.first_day = -1;
    g_window.period_start_day = -1;
    profiling_reset();
    parallel_reset();
}

// Re-reads the manager lists after a (re)load: classifies every decision, resets the per-country
// lists and, with profiling on, points every potential at the private vtable.
void refresh_decisions() {
    uintptr_t manager = decision_manager();
    if (!manager || decisions_unchanged(manager)) {
        return;
    }
    if (!g_decisions.empty()) {
        flush_reports(g_window.last_day);
    }
    reset_statistics();
    g_decisions.clear();
    size_t tag_locked = 0, gated = 0, ai_gated = 0;
    for (uint32_t list : {kListA, kListB}) {
        for (uintptr_t it = list_begin(manager, list), end = list_end(manager, list); it < end; it += 4) {
            const DecisionInfo& info = g_decisions.emplace_back(read_decision(read32(it), list));
            tag_locked += info.locked_country >= 0 ? 1 : 0;
            gated += info.gate ? 1 : 0;
            ai_gated += info.ai_child ? 1 : 0;
        }
    }
    g_lists.assign(game::country_count(), CountryLists{});
    profiling_attach();
    logf("decision_cull: %zu decisions (%zu tag-locked, %zu with a flag gate, %zu with an ai check), %zu countries%s",
         g_decisions.size(), tag_locked, gated, ai_gated, g_lists.size(), g_profiling ? ", potentials timed" : "");
}

void hourly_tick() {
    refresh_decisions();
    if (!g_decisions.empty() && g_lists.size() != game::country_count()) {
        g_lists.assign(game::country_count(), CountryLists{});  // countries were not loaded yet at the first scan
    }
    int32_t day = game::current_day();
    if (day < 0 || day == g_window.last_day || g_decisions.empty()) {
        return;
    }
    if (g_window.first_day < 0) {
        g_window.first_day = day;
        g_window.first_qpc = util::qpc();
        g_window.period_start_day = day;
    } else {
        if (day != g_window.last_day + 1) {
            mark_all_dirty();  // the date jumped: a save was loaded, flags may be anything now
        }
        if (day - g_window.period_start_day >= kReportEveryDays) {
            flush_reports(g_window.last_day);
            g_window.period_start_day = day;
        }
    }
    g_window.last_day = day;
}

// --- install -------------------------------------------------------------------------------------

bool hook_slot(uint32_t slot_rva, uint32_t expected_rva, uintptr_t replacement, uintptr_t* original,
               const char* what) {
    uintptr_t slot = hooks::exe_addr(slot_rva);
    *original = read32(slot);
    if (*original != hooks::exe_addr(expected_rva) || !hooks::replace_pointer(slot, *original, replacement)) {
        logf("decision_cull: %s slot not recognised, feature off", what);
        return false;
    }
    return true;
}

bool install_flag_hooks() {
    uintptr_t original = 0;
    if (!hook_slot(kClrCountryFlagSlotRva, kClrCountryFlagExecuteRva,
                   reinterpret_cast<uintptr_t>(&clr_country_flag_hook), &original, "CClrCountryFlagEffect::Execute")) {
        return false;
    }
    g_original_clr_country_flag = reinterpret_cast<ExecuteFn>(original);
    if (!hook_slot(kClrGlobalFlagSlotRva, kClrGlobalFlagExecuteRva, reinterpret_cast<uintptr_t>(&clr_global_flag_hook),
                   &original, "CClrGlobalFlagEffect::Execute")) {
        return false;  // the country-flag hook alone is harmless: it only marks lists dirty
    }
    g_original_clr_global_flag = reinterpret_cast<ExecuteFn>(original);
    return true;
}

// The flag-clear hooks, the two DecisionManager_Get calls of AI_DiplomacyUpdate and the
// AI_DecisionUpdate call; false (logged) when a site is not the expected one.
bool install_cull_hooks() {
    for (uint32_t site : kGetterCallSites) {
        if (!hooks::expect_bytes(hooks::exe_addr(site), "\xE8", 1)) {
            logf("decision_cull: AI call site %08X not recognised, feature off", hooks::listing_va(site));
            return false;
        }
    }
    if (!install_flag_hooks()) {
        return false;
    }
    uintptr_t ai_decisions = hooks::exe_addr(kAiDecisionsRva);
    if (!hooks::expect_bytes(hooks::exe_addr(kAiDecisionsCallSiteRva), "\xE8", 1) ||
        !hooks::expect_bytes(ai_decisions, "\x55\x8B\xEC\x6A\xFF", 5)) {
        logf("decision_cull: AI_DecisionUpdate call site not recognised, feature off");
        return false;
    }
    uintptr_t getter = hooks::exe_addr(kDecisionManagerGetterRva);
    for (uint32_t site : kGetterCallSites) {
        if (!hooks::redirect_call(hooks::exe_addr(site), getter, reinterpret_cast<uintptr_t>(&getter_stub))) {
            logf("decision_cull: AI call site %08X does not call DecisionManager_Get, feature off",
                 hooks::listing_va(site));
            return false;  // an already redirected site keeps working on its own
        }
    }
    g_original_ai_decisions = reinterpret_cast<AiDecisionsFn>(ai_decisions);
    if (!hooks::redirect_call(hooks::exe_addr(kAiDecisionsCallSiteRva), ai_decisions,
                              reinterpret_cast<uintptr_t>(&ai_decisions_hook))) {
        logf("decision_cull: AI_DecisionUpdate call site does not call it, feature off");
        return false;
    }
    return true;
}

void resolve_vtables() {
    g_and_vtable = hooks::exe_addr(game::kAndTriggerVtableRva);
    g_not_vtable = hooks::exe_addr(game::kNotTriggerVtableRva);
    g_tag_vtable = hooks::exe_addr(kTagVtableRva);
    g_has_country_flag_vtable = hooks::exe_addr(kHasCountryFlagVtableRva);
    g_has_global_flag_vtable = hooks::exe_addr(kHasGlobalFlagVtableRva);
    g_is_ai_vtable = hooks::exe_addr(kIsAiVtableRva);
    g_scope_ctor = reinterpret_cast<game::ScopeCtorFn>(hooks::exe_addr(game::kScopeCtorRva));
}

}  // namespace

std::string decision_name(uintptr_t decision) { return game::string_at(decision + kDecisionName, kNameMax); }

bool lists_stale(const CountryLists& lists, size_t country, int32_t day) {
    return lists.dirty || lists.built_day < 0 || day - lists.built_day >= kRefreshDays ||
           lists.controller != controller_of(country);
}

void build_lists(size_t country, int32_t day) {
    CountryLists& lists = g_lists[country];
    int64_t start = util::qpc();
    lists.a.clear();
    lists.b.clear();
    lists.built_day = day;
    lists.controller = controller_of(country);
    lists.version++;
    lists.dirty = false;

    uintptr_t object = game::country_or_null(country);
    if (!object) {
        return;  // empty lists: the AI loop has nothing to do for a country that does not exist
    }
    alignas(16) unsigned char scope[game::kScopeBuffer];
    std::memset(scope, 0, sizeof(scope));
    g_scope_ctor(scope, nullptr, nullptr);
    std::memcpy(scope + game::kScopeTag, reinterpret_cast<const void*>(object + game::kCountryTag), 8);  // {tag, index}

    for (const DecisionInfo& d : g_decisions) {
        if (d.locked_country >= 0 && static_cast<size_t>(d.locked_country) != country) {
            g_cull_stats.removed_tag++;
        } else if (d.ai_child && !game::evaluate(d.ai_child, scope)) {
            g_cull_stats.removed_ai++;
        } else if (d.gate && !game::evaluate(d.gate, scope)) {
            g_cull_stats.removed_dead++;
        } else {
            (d.list == 0 ? lists.a : lists.b).push_back(d.decision);
        }
    }
    g_cull_stats.list_builds++;
    g_cull_stats.list_build_ticks += static_cast<uint64_t>(util::qpc() - start);
}

void set_fake_list(uint32_t offset, const std::vector<uintptr_t>& list) {
    uintptr_t begin = list.empty() ? 0 : reinterpret_cast<uintptr_t>(list.data());
    uintptr_t end = begin + list.size() * sizeof(uintptr_t);
    *reinterpret_cast<uintptr_t*>(g_fake_manager + offset) = begin;
    *reinterpret_cast<uintptr_t*>(g_fake_manager + offset + 4) = end;
    *reinterpret_cast<uintptr_t*>(g_fake_manager + offset + 8) = end;
}

double ms(uint64_t ticks) { return util::qpc_ms(static_cast<int64_t>(ticks)); }

}  // namespace cp::features::decisions

namespace cp::features {

void install_decision_cull(const Config& config) {
    using namespace decisions;
    g_cull = config.cull_decision_checks;
    g_profiling = config.decision_profiling;
    int parallel = resolve_parallel_mode(config);
    if (!g_cull && !g_profiling) {
        logf("decision_cull: disabled");
        return;
    }
    resolve_vtables();
    if (!profiling_install(g_and_vtable) ||
        !hooks::expect_bytes(reinterpret_cast<uintptr_t>(g_scope_ctor), "\x55\x8B\xEC", 3)) {
        logf("decision_cull: CAndTrigger::Evaluate / CEventScope ctor not recognised, feature off");
        return;
    }
    if (g_cull && !install_cull_hooks()) {
        return;
    }
    if (!hooks::on_hourly_tick(&hourly_tick)) {
        logf("decision_cull: hourly tick hook unavailable, feature off");
        return;
    }
    logf("decision_cull: active (cull %s: per-country AI lists, rebuilt on flag clears and every %d days; "
         "profiling %s)",
         g_cull ? "on" : "off", kRefreshDays, g_profiling ? "on" : "off");
    if (parallel) {
        logf("parallel_decisions: active (%s): "
             "decision checks of the AI queue evaluated on worker threads at max speed",
             parallel == 2 ? "shadow mode, the game still uses its own evaluation" : "results used");
    }
}

}  // namespace cp::features
