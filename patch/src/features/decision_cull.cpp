#include "features/decision_cull.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "hooks/hourly_tick.h"
#include "hooks/memory.h"
#include "log.h"
#include "parallel/worker_pool.h"

// Background (re/systems/decisions.md, docs/decision-cull.md, re/raw/decision-keyword-map.txt):
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
// decision_profiling swaps the vtable pointer of every decision's `potential` (a CAndTrigger embedded
// at CDecision+0x24) for a private copy whose Evaluate slot times the call; off, nothing is swapped.

namespace cp::features {

namespace {

// --- v2game.exe 3.04 (RVA = VA - 0x00400000) ---------------------------------------------------
constexpr uint32_t kWorldPtrRva = 0xE588E8;              // DAT_012588E8
constexpr uint32_t kWorldDateOffset = 0xB0C;             // hours
constexpr uint32_t kCountryManagerPtrRva = 0xE587E4;     // DAT_012587E4, +0 count of tags, +4 CCountry* array
constexpr uint32_t kDecisionManagerPtrRva = 0xF19730;    // DAT_01319730; lists at +0xC/+0x10 and +0x28/+0x2C
constexpr uint32_t kDecisionManagerGetterRva = 0x475040; // DecisionManager_Get()
constexpr uint32_t kGetterCallSites[] = {0x45D34F, 0x45D459};  // in AI_DiplomacyUpdate
constexpr uint32_t kAiDecisionsRva = 0x421190;            // AI_DecisionUpdate(ai): the daily per-country loop (frame-driven)
constexpr uint32_t kAiDecisionsCallSiteRva = 0x41EA9B;    // in FUN_0081E9E0, `MOV ECX,ESI; CALL`; ESI = AI object
constexpr uint32_t kAiObjectCountryIndex = 0x34;          // AI object: +0x30 tag, +0x34 country index
constexpr uint32_t kScopeCtorRva = 0x4A8650;             // CEventScope::ctor(this, parent = 0)
constexpr uint32_t kAndVtableRva = 0x9F9994;             // CAndTrigger::vftable
constexpr uint32_t kNotVtableRva = 0xA2FBA8;             // CNotTrigger::vftable
constexpr uint32_t kTagVtableRva = 0xA30C80;             // CTagTrigger::vftable (+0x1C tag, +0x20 country index)
constexpr uint32_t kHasCountryFlagVtableRva = 0xA307E8;  // CHasCountryFlagTrigger::vftable
constexpr uint32_t kHasGlobalFlagVtableRva = 0xA30820;   // CHasGlobalFlagTrigger::vftable
constexpr uint32_t kIsAiVtableRva = 0xA30EE8;            // CIsAiTrigger::vftable (`ai = yes/no`, value at +0x1C)
constexpr uint32_t kClrCountryFlagSlotRva = 0xA2ACC0 + 0x2C;  // CClrCountryFlagEffect::vftable slot 11 (Execute)
constexpr uint32_t kClrCountryFlagExecuteRva = 0x482A40;
constexpr uint32_t kClrGlobalFlagSlotRva = 0xA2ADA0 + 0x2C;   // CClrGlobalFlagEffect::vftable slot 11 (Execute)
constexpr uint32_t kClrGlobalFlagExecuteRva = 0x482BA0;

constexpr uint32_t kManagerSize = 0x38;       // DecisionManager_Get allocates 0x38 bytes
constexpr uint32_t kListA = 0x0C;             // vector<CDecision*>: begin +0, end +4, capacity +8
constexpr uint32_t kListB = 0x28;
constexpr uint32_t kCountryTag = 0x1C;        // CCountry
constexpr uint32_t kCountryIndex = 0x20;
constexpr uint32_t kDecisionName = 0x08;      // std::string (SSO): buffer/pointer +0x08, size +0x18, capacity +0x1C
constexpr uint32_t kDecisionPotential = 0x24; // embedded CAndTrigger
constexpr uint32_t kBlockBegin = 0x08;        // trigger block: vector<CTrigger*> begin
constexpr uint32_t kBlockEnd = 0x0C;          //                                 end
constexpr uint32_t kTriggerThisFlag = 0x18;   // value was `THIS`
constexpr uint32_t kTriggerFromFlag = 0x19;   // value was `FROM`
constexpr uint32_t kTagTriggerIndex = 0x20;   // CTagTrigger: country index of the literal tag
constexpr uint32_t kVtableEvaluate = 0x18;    // CTrigger slot 6: bool Evaluate(scope)
constexpr uint32_t kScopeTag = 0x08;          // CEventScope: country tag
constexpr uint32_t kScopeIndex = 0x0C;        //              country index
constexpr size_t kScopeSize = 0x60;
constexpr size_t kVtableSlotsCopied = 16;     // CAndTrigger has 12 used slots; extra entries are never called

constexpr int32_t kReportEveryDays = 30;  // profiling report period
constexpr int32_t kRefreshDays = 90;      // a country's lists are rebuilt this often to drop newly dead pairs
constexpr uint64_t kSample = 16;          // profiling: 1 in 16 evaluations is timed (power of two)

// __thiscall in the game; __fastcall takes `this` in ecx and ignores edx.
using EvaluateFn = char(__fastcall*)(void* self, void* edx, void* scope);
// CEffect::Execute(scope, arg2) is __thiscall with two stack arguments (`RET 0x8`); the second one is passed through.
using ExecuteFn = void(__fastcall*)(void* self, void* edx, void* scope, void* arg2);
using ScopeCtorFn = void*(__fastcall*)(void* self, void* edx, void* parent);

bool g_cull = false;
bool g_profiling = false;
EvaluateFn g_original_evaluate = nullptr;
ExecuteFn g_original_clr_country_flag = nullptr;
ExecuteFn g_original_clr_global_flag = nullptr;
ScopeCtorFn g_scope_ctor = nullptr;
uintptr_t g_fake_vtable[kVtableSlotsCopied];
uintptr_t g_and_vtable = 0;
uintptr_t g_not_vtable = 0;
uintptr_t g_tag_vtable = 0;
uintptr_t g_has_country_flag_vtable = 0;
uintptr_t g_has_global_flag_vtable = 0;
uintptr_t g_is_ai_vtable = 0;
int64_t g_qpc_freq = 0;

struct DecisionInfo {
    uintptr_t decision = 0;
    uint8_t list = 0;             // 0 = manager list A (+0x0C), 1 = list B (+0x28)
    int32_t locked_country = -1;  // top-level `tag = X` with a literal tag: only X can ever pass
    uintptr_t gate = 0;           // top-level NOT block made only of has_country_flag / has_global_flag
    uintptr_t ai_child = 0;       // top-level `ai = yes/no`
    // profiling, accumulated since the decisions were loaded (sampled: calls, passed, ticks)
    uint64_t calls = 0;
    uint64_t passed = 0;
    uint64_t ticks = 0;
};
std::vector<DecisionInfo> g_decisions;
size_t g_tag_locked = 0;
size_t g_gated = 0;
size_t g_ai_gated = 0;

// potential address -> decision index (profiling only); open addressing, usual probe length one.
struct LookupSlot {
    uintptr_t key = 0;
    uint32_t index = 0;
};
std::vector<LookupSlot> g_lookup;
uint32_t g_lookup_shift = 32;

// Per country: the two filtered lists the AI loop sees, and when they were built.
struct CountryLists {
    std::vector<uintptr_t> a;  // replaces manager list A
    std::vector<uintptr_t> b;  // replaces manager list B
    int32_t built_day = -1;
    uint32_t version = 0;  // bumped on every build; parallel results are only used for the version they saw
    bool dirty = true;
};
std::vector<CountryLists> g_lists;
alignas(16) uint8_t g_fake_manager[kManagerSize];

// cull statistics since load (exact)
uint64_t g_list_builds = 0;
uint64_t g_list_build_ticks = 0;
uint64_t g_removed_tag = 0;   // decisions left out of a list, summed over builds
uint64_t g_removed_ai = 0;
uint64_t g_removed_dead = 0;
uint64_t g_ai_requests = 0;   // getter calls served with a filtered list
uint64_t g_ai_decisions_seen = 0;  // list entries handed to the AI loop, summed over requests

// profiling totals since load
uint64_t g_total_calls = 0;  // exact: evaluations that went through the timed hook
uint64_t g_sample_counter = 0;
int32_t g_first_day = -1;
int64_t g_first_qpc = 0;
int32_t g_last_day = -1;
int32_t g_period_start_day = -1;
std::wstring g_report_path;

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

uintptr_t country_object(size_t index) {
    uintptr_t manager = read32(hooks::exe_addr(kCountryManagerPtrRva));
    return manager && index < read32(manager) ? read32(read32(manager + 4) + index * 4) : 0;
}

uintptr_t decision_manager() { return read32(hooks::exe_addr(kDecisionManagerPtrRva)); }

std::string decision_name(uintptr_t decision) {
    uint32_t size = read32(decision + kDecisionName + 0x10);
    uint32_t capacity = read32(decision + kDecisionName + 0x14);
    const char* data = capacity > 15 ? reinterpret_cast<const char*>(read32(decision + kDecisionName))
                                     : reinterpret_cast<const char*>(decision + kDecisionName);
    return std::string(data, size > 64 ? 64 : size);
}

inline bool evaluate(uintptr_t trigger, void* scope) {
    auto fn = reinterpret_cast<EvaluateFn>(read32(read32(trigger) + kVtableEvaluate));
    return fn(reinterpret_cast<void*>(trigger), nullptr, scope) != 0;
}

// --- static classification of one potential block -------------------------------------------------

bool block_is_only_flag_checks(uintptr_t block) {
    uintptr_t begin = read32(block + kBlockBegin), end = read32(block + kBlockEnd);
    if (begin == end) {
        return false;
    }
    for (uintptr_t it = begin; it < end; it += 4) {
        uintptr_t child = read32(it);
        uintptr_t vtable = read32(child);
        if (vtable != g_has_country_flag_vtable && vtable != g_has_global_flag_vtable) {
            return false;
        }
        if (read8(child + kTriggerThisFlag) != 0 || read8(child + kTriggerFromFlag) != 0) {
            return false;
        }
    }
    return true;
}

void classify(DecisionInfo& info) {
    uintptr_t block = info.decision + kDecisionPotential;
    for (uintptr_t it = read32(block + kBlockBegin), end = read32(block + kBlockEnd); it < end; it += 4) {
        uintptr_t child = read32(it);
        uintptr_t vtable = read32(child);
        if (vtable == g_tag_vtable && info.locked_country < 0 && read8(child + kTriggerThisFlag) == 0 &&
            read8(child + kTriggerFromFlag) == 0) {
            info.locked_country = static_cast<int32_t>(read32(child + kTagTriggerIndex));
        } else if (vtable == g_not_vtable && info.gate == 0 && block_is_only_flag_checks(child)) {
            info.gate = child;
        } else if (vtable == g_is_ai_vtable && info.ai_child == 0 && read8(child + kTriggerThisFlag) == 0) {
            info.ai_child = child;
        }
    }
}

// --- per-country lists -----------------------------------------------------------------------------

void build_lists(size_t country, int32_t day) {
    CountryLists& lists = g_lists[country];
    int64_t start = qpc();
    lists.a.clear();
    lists.b.clear();
    lists.built_day = day;
    lists.version++;
    lists.dirty = false;

    uintptr_t object = country_object(country);
    if (!object) {
        return;  // empty lists: the AI loop has nothing to do for a country that does not exist
    }
    alignas(16) unsigned char scope[kScopeSize];
    std::memset(scope, 0, sizeof(scope));
    g_scope_ctor(scope, nullptr, nullptr);
    std::memcpy(scope + kScopeTag, reinterpret_cast<const void*>(object + kCountryTag), 8);  // {tag, index}

    for (const DecisionInfo& d : g_decisions) {
        if (d.locked_country >= 0 && static_cast<size_t>(d.locked_country) != country) {
            g_removed_tag++;
            continue;
        }
        if (d.ai_child && !evaluate(d.ai_child, scope)) {
            g_removed_ai++;
            continue;
        }
        if (d.gate && !evaluate(d.gate, scope)) {
            g_removed_dead++;
            continue;
        }
        (d.list == 0 ? lists.a : lists.b).push_back(d.decision);
    }
    g_list_builds++;
    g_list_build_ticks += static_cast<uint64_t>(qpc() - start);
}

void set_fake_list(uint32_t offset, const std::vector<uintptr_t>& list) {
    uintptr_t begin = list.empty() ? 0 : reinterpret_cast<uintptr_t>(list.data());
    uintptr_t end = begin + list.size() * sizeof(uintptr_t);
    *reinterpret_cast<uintptr_t*>(g_fake_manager + offset) = begin;
    *reinterpret_cast<uintptr_t*>(g_fake_manager + offset + 4) = end;
    *reinterpret_cast<uintptr_t*>(g_fake_manager + offset + 8) = end;
}

// Called in place of DecisionManager_Get() from the AI loop; `country` is the AI's country index.
uintptr_t __cdecl manager_for_country(uint32_t country) {
    uintptr_t real = decision_manager();
    int32_t day = current_day();
    if (!real || day < 0 || g_decisions.empty() || country >= g_lists.size()) {
        return real;
    }
    CountryLists& lists = g_lists[country];
    if (lists.dirty || lists.built_day < 0 || day - lists.built_day >= kRefreshDays) {
        build_lists(country, day);
    }
    std::memcpy(g_fake_manager, reinterpret_cast<const void*>(real), kManagerSize);
    set_fake_list(kListA, lists.a);
    set_fake_list(kListB, lists.b);
    g_ai_requests++;
    g_ai_decisions_seen += lists.a.size() + lists.b.size();
    return reinterpret_cast<uintptr_t>(g_fake_manager);
}

// Called through a pointer so the compiler keeps the declared __cdecl convention for the asm call.
uintptr_t(__cdecl* volatile g_manager_for_country)(uint32_t) = &manager_for_country;

// The getter takes no arguments; the AI object is in ESI at both call sites (+0xC = country index).
__declspec(naked) void getter_stub() {
    __asm {
        mov eax, dword ptr [esi + 0xC]
        push eax
        call dword ptr [g_manager_for_country]
        add esp, 4
        ret
    }
}

// --- parallel_decisions ----------------------------------------------------------------------------
//
// AI_DecisionUpdate, per decision of its lists: CDecision slot 6 (twice; `MOV AL,1 / RET` in 3.04),
// potential.Evaluate(scope), allow.Evaluate(scope), then ai_will_do, CRT rand() and, on success, a
// session command; the game itself is only changed later, when the command runs. Its scope is
// CEventScope::ctor(scope, 0x2D) with {tag, index} of the AI object (+0x30/+0x34) at scope+0x08.
//
// At the start of a frame in which fast_ai_queue drains the AI queue, the countries waiting in it are
// read from the queue, and the worker pool evaluates slot 6, potential and allow of every decision of
// each country's list with that same scope. During the frame, AI_DecisionUpdate gets a list of only
// the decisions that passed, in the original order, so ai_will_do, rand() and the command run as before
// in the game's own code. Shadow mode (2) leaves the game on the full lists and compares instead.

constexpr uint32_t kCountryAiObject = 0x208;     // CCountry: AI object, 0 for none (AI_FrameUpdate skips those)
constexpr uint32_t kAiObjectTag = 0x30;
constexpr uint32_t kDecisionAllow = 0x40;        // embedded CAndTrigger
constexpr uint32_t kDecisionCheckSlot = 0x18;    // CDecision slot 6
constexpr uint32_t kDecisionCheckTrivialRva = 0x631490;  // MOV AL,1 / RET
constexpr uintptr_t kAiScopeParent = 0x2D;       // AI_DecisionUpdate: PUSH 0x2D at 0x008211B3
constexpr uint32_t kQueueNodeCountry = 0x04;     // AI job node: +0 tag, +4 country index, +8 prev, +0xC next
constexpr uint32_t kQueueNodeNext = 0x0C;
constexpr int kMaxMismatchLogs = 20;

int g_parallel = 0;  // 0 off, 1 on, 2 shadow
uintptr_t g_decision_check_trivial = 0;

struct Precomputed {
    std::vector<uintptr_t> a;  // decisions of the country's list A whose checks passed, in list order
    std::vector<uintptr_t> b;
    uintptr_t ai = 0;
    uint32_t lists_version = 0;
    uint32_t frame = 0;        // frame of the computation; 0 = never
    uint32_t queued = 0;       // frame whose batch includes this country
    bool ok = false;           // false when a decision had an unexpected slot 6
};
std::vector<Precomputed> g_pre;
std::vector<uint32_t> g_pending;  // countries of the current frame's batch
uint32_t g_frame = 0;
bool g_frame_open = false;

// parallel statistics since load
uint64_t g_par_frames = 0;
uint64_t g_par_countries = 0;
uint64_t g_par_ticks = 0;       // wall time of the batches on the game thread
uint64_t g_par_checked = 0;     // decisions evaluated by the batches
uint64_t g_par_passed = 0;
uint64_t g_par_used = 0;        // AI updates served from a batch
uint64_t g_shadow_compared = 0; // decisions compared in shadow mode
uint64_t g_shadow_mismatches = 0;
int g_mismatch_logs = 0;

void make_ai_scope(unsigned char* scope, uintptr_t ai) {
    std::memset(scope, 0, kScopeSize);
    g_scope_ctor(scope, nullptr, reinterpret_cast<void*>(kAiScopeParent));
    std::memcpy(scope + kScopeTag, reinterpret_cast<const void*>(ai + kAiObjectTag), 8);  // {tag, index}
}

// 1 = passes, 0 = fails, -1 = slot 6 is not the expected `return true`.
inline int decision_passes(uintptr_t decision, void* scope) {
    if (read32(read32(decision) + kDecisionCheckSlot) != g_decision_check_trivial) {
        return -1;
    }
    return evaluate(decision + kDecisionPotential, scope) && evaluate(decision + kDecisionAllow, scope) ? 1 : 0;
}

// Worker task: one country. Reads the game and g_lists; writes only its own Precomputed, whose vectors
// were reserved by the game thread, so nothing is allocated here.
void precompute_country(int index, void*) {
    uint32_t country = g_pending[static_cast<size_t>(index)];
    Precomputed& p = g_pre[country];
    const CountryLists& lists = g_lists[country];
    alignas(16) unsigned char scope[kScopeSize];
    make_ai_scope(scope, p.ai);
    p.ok = true;
    for (int which = 0; which < 2 && p.ok; which++) {
        const std::vector<uintptr_t>& in = which == 0 ? lists.a : lists.b;
        std::vector<uintptr_t>& out = which == 0 ? p.a : p.b;
        for (uintptr_t decision : in) {
            int r = decision_passes(decision, scope);
            if (r < 0) {
                p.ok = false;
                break;
            }
            if (r) {
                out.push_back(decision);
            }
        }
    }
    p.frame = g_frame;
}

void frame_begin(void* queue_ref) {
    if (!g_parallel || g_decisions.empty() || !queue_ref) {
        return;
    }
    g_frame++;
    g_frame_open = true;
    uintptr_t bucket = read32(reinterpret_cast<uintptr_t>(queue_ref));
    int32_t day = current_day();
    if (!bucket || day < 0) {
        return;
    }
    if (g_pre.size() != g_lists.size()) {
        g_pre.assign(g_lists.size(), Precomputed{});
    }
    int64_t start = qpc();
    g_pending.clear();
    uint64_t checks = 0;
    // Bucket 0 is the one AI_FrameUpdate drains (the only one in single player).
    for (uintptr_t node = read32(bucket); node; node = read32(node + kQueueNodeNext)) {
        uint32_t country = read32(node + kQueueNodeCountry);
        uintptr_t object = country < g_lists.size() ? country_object(country) : 0;
        uintptr_t ai = object ? read32(object + kCountryAiObject) : 0;
        if (!ai || g_pre[country].queued == g_frame) {
            continue;
        }
        CountryLists& lists = g_lists[country];
        if (lists.dirty || lists.built_day < 0 || day - lists.built_day >= kRefreshDays) {
            build_lists(country, day);  // the same rebuild the AI update would do, done here on the game thread
        }
        Precomputed& p = g_pre[country];
        p.a.clear();
        p.b.clear();
        p.a.reserve(lists.a.size());
        p.b.reserve(lists.b.size());
        p.ai = ai;
        p.lists_version = lists.version;
        p.frame = 0;
        p.queued = g_frame;
        g_pending.push_back(country);
        checks += lists.a.size() + lists.b.size();
    }
    if (g_pending.empty()) {
        return;
    }
    parallel::run(static_cast<int>(g_pending.size()), &precompute_country, nullptr);
    for (uint32_t country : g_pending) {
        g_par_passed += g_pre[country].a.size() + g_pre[country].b.size();
    }
    g_par_frames++;
    g_par_countries += g_pending.size();
    g_par_checked += checks;
    g_par_ticks += static_cast<uint64_t>(qpc() - start);
}

void frame_end() { g_frame_open = false; }

// The batch result for `country`, when it is still valid for this AI update.
const Precomputed* precomputed_for(uint32_t country, uintptr_t ai) {
    if (!g_frame_open || country >= g_pre.size()) {
        return nullptr;
    }
    const Precomputed& p = g_pre[country];
    if (p.frame != g_frame || !p.ok || p.ai != ai || p.lists_version != g_lists[country].version) {
        return nullptr;
    }
    return &p;
}

// Shadow mode: evaluates the country's list here, on the game thread, right before the game does,
// and compares with the batch.
void shadow_compare(uint32_t country, uintptr_t ai, const Precomputed& p) {
    const CountryLists& lists = g_lists[country];
    alignas(16) unsigned char scope[kScopeSize];
    make_ai_scope(scope, ai);
    for (int which = 0; which < 2; which++) {
        const std::vector<uintptr_t>& in = which == 0 ? lists.a : lists.b;
        const std::vector<uintptr_t>& batch = which == 0 ? p.a : p.b;
        size_t at = 0;
        for (uintptr_t decision : in) {
            bool in_batch = at < batch.size() && batch[at] == decision;
            if (in_batch) {
                at++;
            }
            bool serial = decision_passes(decision, scope) == 1;
            g_shadow_compared++;
            if (serial != in_batch) {
                g_shadow_mismatches++;
                if (g_mismatch_logs++ < kMaxMismatchLogs) {
                    uintptr_t object = country_object(country);
                    char tag[4] = {};
                    if (object) {
                        std::memcpy(tag, reinterpret_cast<const void*>(object + kCountryTag), 3);
                    }
                    logf("parallel_decisions: MISMATCH day %d %s (%u) %s: serial %d, parallel %d", current_day(), tag,
                         country, decision_name(decision).c_str(), serial ? 1 : 0, in_batch ? 1 : 0);
                }
            }
        }
    }
}

void log_parallel_summary(int32_t last_day) {
    int32_t days = last_day - g_first_day + 1;
    if (!g_parallel || g_first_day < 0 || days <= 0 || g_par_frames == 0) {
        return;
    }
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    logf("parallel_decisions: days %d-%d: %llu batches, %.0f countries and %.0f checks per batch, %.2f ms/day on the game "
         "thread (%d workers + game thread), %llu passed/day, %llu AI updates served",
         g_first_day, last_day, ull(g_par_frames), static_cast<double>(g_par_countries) / g_par_frames,
         static_cast<double>(g_par_checked) / g_par_frames, ms(g_par_ticks) / days, parallel::workers(),
         ull(g_par_passed / days), ull(g_par_used));
    if (g_parallel == 2) {
        logf("parallel_decisions: shadow: %llu checks compared, %llu mismatches", ull(g_shadow_compared),
             ull(g_shadow_mismatches));
    }
}

// AI_DecisionUpdate reads the manager through the global, so the global itself is pointed at the
// country's copy for the duration of the call and restored afterwards, also on an unwind.
using AiDecisionsFn = int(__fastcall*)(void* ai, void* edx);
AiDecisionsFn g_original_ai_decisions = nullptr;

int __fastcall ai_decisions_hook(void* ai, void* edx) {
    uintptr_t* global = reinterpret_cast<uintptr_t*>(hooks::exe_addr(kDecisionManagerPtrRva));
    uintptr_t real = *global;
    uint32_t country = read32(reinterpret_cast<uintptr_t>(ai) + kAiObjectCountryIndex);
    uintptr_t replacement = manager_for_country(country);
    if (replacement != real && g_parallel) {
        if (const Precomputed* p = precomputed_for(country, reinterpret_cast<uintptr_t>(ai))) {
            if (g_parallel == 1) {
                set_fake_list(kListA, p->a);
                set_fake_list(kListB, p->b);
            } else {
                shadow_compare(country, reinterpret_cast<uintptr_t>(ai), *p);
            }
            g_par_used++;
        }
    }
    if (replacement == real) {
        return g_original_ai_decisions(ai, edx);
    }
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

inline uint32_t scope_country(void* scope) { return read32(reinterpret_cast<uintptr_t>(scope) + kScopeIndex); }

void __fastcall clr_country_flag_hook(void* self, void*, void* scope, void* arg2) {
    g_original_clr_country_flag(self, nullptr, scope, arg2);
    mark_dirty(scope_country(scope));  // rebuilt on the next AI update, never while a loop may be running
}

void __fastcall clr_global_flag_hook(void* self, void*, void* scope, void* arg2) {
    g_original_clr_global_flag(self, nullptr, scope, arg2);
    mark_all_dirty();
}

// --- profiling hook (only installed with decision_profiling) ---------------------------------------

inline uint32_t lookup_hash(uintptr_t key) {
    return static_cast<uint32_t>((key >> 2) * 0x9E3779B1u) >> g_lookup_shift;
}

void lookup_build() {
    size_t size = 64;
    uint32_t shift = 26;
    while (size < g_decisions.size() * 4) {
        size <<= 1;
        shift--;
    }
    g_lookup.assign(size, LookupSlot{});
    g_lookup_shift = shift;
    for (size_t i = 0; i < g_decisions.size(); i++) {
        uintptr_t key = g_decisions[i].decision + kDecisionPotential;
        uint32_t at = lookup_hash(key);
        while (g_lookup[at].key != 0) {
            at = (at + 1) & static_cast<uint32_t>(size - 1);
        }
        g_lookup[at] = {key, static_cast<uint32_t>(i)};
    }
}

inline DecisionInfo* lookup(uintptr_t key) {
    if (g_lookup.empty()) {
        return nullptr;
    }
    uint32_t mask = static_cast<uint32_t>(g_lookup.size() - 1);
    for (uint32_t at = lookup_hash(key);; at = (at + 1) & mask) {
        const LookupSlot& slot = g_lookup[at];
        if (slot.key == key) {
            return &g_decisions[slot.index];
        }
        if (slot.key == 0) {
            return nullptr;
        }
    }
}

char __fastcall potential_hook(void* self, void*, void* scope) {
    g_total_calls++;
    bool sampled = (g_sample_counter++ & (kSample - 1)) == 0;
    if (!sampled) {
        return g_original_evaluate(self, nullptr, scope);
    }
    int64_t start = qpc();
    char result = g_original_evaluate(self, nullptr, scope);
    uint64_t ticks = static_cast<uint64_t>(qpc() - start);
    if (DecisionInfo* info = lookup(reinterpret_cast<uintptr_t>(self))) {
        info->ticks += ticks;
        info->calls++;
        info->passed += result ? 1 : 0;
    }
    return result;
}

// --- reporting -----------------------------------------------------------------------------------

void log_cull_summary(int32_t last_day) {
    int32_t days = last_day - g_first_day + 1;
    if (g_first_day < 0 || days <= 0 || g_ai_requests == 0) {
        return;
    }
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    double per_request = static_cast<double>(g_ai_decisions_seen) / static_cast<double>(g_ai_requests);
    logf("decision_cull: days %d-%d: AI loop saw %.0f of %zu decisions per country on average (%llu list requests/day); "
         "%llu list builds (%.2f ms/day) left out %llu by tag, %llu by ai, %llu dead per build",
         g_first_day, last_day, per_request, g_decisions.size(), ull(g_ai_requests / days),
         ull(g_list_builds), ms(g_list_build_ticks) / days,
         ull(g_list_builds ? g_removed_tag / g_list_builds : 0), ull(g_list_builds ? g_removed_ai / g_list_builds : 0),
         ull(g_list_builds ? g_removed_dead / g_list_builds : 0));
}

void write_report(int32_t last_day) {
    int32_t days = last_day - g_first_day + 1;
    if (g_first_day < 0 || days <= 0 || g_decisions.empty()) {
        return;
    }
    uint64_t calls = 0, passed = 0, ticks = 0;
    for (const DecisionInfo& d : g_decisions) {
        calls += d.calls;
        passed += d.passed;
        ticks += d.ticks;
    }
    double wall_ms_day = ms(static_cast<uint64_t>(qpc() - g_first_qpc)) / days;
    double eval_ms_day = ms(ticks) * static_cast<double>(kSample) / days;
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };

    logf("decision_profiling: days %d-%d (%d days, %.1f ms/day wall): %llu potential evaluations/day, %.2f ms/day, "
         "%llu passed/day -> v2cp_decisions.log",
         g_first_day, last_day, days, wall_ms_day, ull(g_total_calls / days), eval_ms_day, ull(passed * kSample / days));

    FILE* out = nullptr;
    if (_wfopen_s(&out, g_report_path.c_str(), L"w") != 0 || !out) {
        logf("decision_profiling: cannot write v2cp_decisions.log");
        return;
    }
    fprintf(out,
            "# decision profiling: running per-day averages since the decisions were loaded; rewritten every %d game days.\n"
            "# days %d-%d (%d days), wall %.1f ms/day, cull_decision_checks=%d\n"
            "# %llu potential evaluations/day costing %.2f ms/day (1 in %llu timed), %llu passed/day\n"
            "# %zu decisions: %zu tag-locked (T), %zu with a flag gate (F), %zu with a top-level ai check (A)\n",
            kReportEveryDays, g_first_day, last_day, days, wall_ms_day, g_cull ? 1 : 0, ull(g_total_calls / days),
            eval_ms_day, ull(kSample), ull(passed * kSample / days), g_decisions.size(), g_tag_locked, g_gated,
            g_ai_gated);
    if (g_cull && g_ai_requests && g_list_builds) {
        fprintf(out,
                "# cull: AI loop saw %.0f decisions per country on average; list builds left out %llu by tag, %llu by ai, "
                "%llu dead per build (%llu builds, %.2f ms/day)\n",
                static_cast<double>(g_ai_decisions_seen) / static_cast<double>(g_ai_requests),
                ull(g_removed_tag / g_list_builds), ull(g_removed_ai / g_list_builds), ull(g_removed_dead / g_list_builds),
                ull(g_list_builds), ms(g_list_build_ticks) / days);
    }
    fprintf(out, "#\n#   ms/day  evals/day  ns/eval  passed/day  TFA  decision\n");

    std::vector<size_t> order(g_decisions.size());
    for (size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
        if (g_decisions[a].ticks != g_decisions[b].ticks) {
            return g_decisions[a].ticks > g_decisions[b].ticks;
        }
        return g_decisions[a].calls > g_decisions[b].calls;
    });
    for (size_t i : order) {
        const DecisionInfo& d = g_decisions[i];
        double ns = d.calls ? 1e6 * ms(d.ticks) / static_cast<double>(d.calls) : 0.0;
        fprintf(out, "%9.3f %10llu %8.0f %11llu  %c%c%c  %s\n", ms(d.ticks) * static_cast<double>(kSample) / days,
                ull(d.calls * kSample / days), ns, ull(d.passed * kSample / days), d.locked_country >= 0 ? 'T' : '-',
                d.gate ? 'F' : '-', d.ai_child ? 'A' : '-', decision_name(d.decision).c_str());
    }
    fclose(out);
}

// --- bookkeeping, once per game hour ------------------------------------------------------------

uintptr_t list_begin(uintptr_t manager, uint32_t list) { return read32(manager + list); }
uintptr_t list_end(uintptr_t manager, uint32_t list) { return read32(manager + list + 4); }

// Re-reads the manager lists after a (re)load: classifies every decision, resets the per-country
// lists and, with profiling on, points every potential at the private vtable.
void refresh_decisions() {
    uintptr_t manager = decision_manager();
    if (!manager) {
        return;
    }
    size_t count = 0;
    for (uint32_t list : {kListA, kListB}) {
        count += (list_end(manager, list) - list_begin(manager, list)) / 4;
    }
    bool same = g_decisions.size() == count;
    size_t n = 0;
    for (uint32_t list : {kListA, kListB}) {
        for (uintptr_t it = list_begin(manager, list), end = list_end(manager, list); same && it < end; it += 4, n++) {
            same = g_decisions[n].decision == read32(it);
        }
    }
    if (same) {
        return;
    }
    if (!g_decisions.empty()) {
        if (g_profiling) {
            write_report(g_last_day);
        }
        if (g_cull) {
            log_cull_summary(g_last_day);
            log_parallel_summary(g_last_day);
        }
    }
    g_decisions.clear();
    g_tag_locked = g_gated = g_ai_gated = 0;
    g_total_calls = 0;
    g_list_builds = g_list_build_ticks = g_removed_tag = g_removed_ai = g_removed_dead = 0;
    g_ai_requests = g_ai_decisions_seen = 0;
    g_par_frames = g_par_countries = g_par_ticks = g_par_checked = g_par_passed = g_par_used = 0;
    g_shadow_compared = g_shadow_mismatches = 0;
    g_pre.clear();
    g_first_day = -1;
    g_period_start_day = -1;
    for (uint32_t list : {kListA, kListB}) {
        for (uintptr_t it = list_begin(manager, list), end = list_end(manager, list); it < end; it += 4) {
            DecisionInfo info;
            info.decision = read32(it);
            info.list = list == kListA ? 0 : 1;
            uintptr_t potential = info.decision + kDecisionPotential;
            uintptr_t vtable = read32(potential);
            if (vtable != g_and_vtable && vtable != reinterpret_cast<uintptr_t>(g_fake_vtable)) {
                // unexpected layout: keep it in every list unclassified so the game still sees it
                g_decisions.push_back(info);
                continue;
            }
            classify(info);
            g_tag_locked += info.locked_country >= 0 ? 1 : 0;
            g_gated += info.gate ? 1 : 0;
            g_ai_gated += info.ai_child ? 1 : 0;
            g_decisions.push_back(info);
        }
    }
    g_lists.assign(country_count(), CountryLists{});
    if (g_profiling) {
        lookup_build();
        for (const DecisionInfo& d : g_decisions) {
            uintptr_t potential = d.decision + kDecisionPotential;
            if (read32(potential) == g_and_vtable) {
                *reinterpret_cast<uintptr_t*>(potential) = reinterpret_cast<uintptr_t>(g_fake_vtable);
            }
        }
    }
    logf("decision_cull: %zu decisions (%zu tag-locked, %zu with a flag gate, %zu with an ai check), %zu countries%s",
         g_decisions.size(), g_tag_locked, g_gated, g_ai_gated, g_lists.size(), g_profiling ? ", potentials timed" : "");
}

void hourly_tick() {
    refresh_decisions();
    if (!g_decisions.empty() && g_lists.size() != country_count()) {
        g_lists.assign(country_count(), CountryLists{});  // countries were not loaded yet at the first scan
    }
    int32_t day = current_day();
    if (day < 0 || day == g_last_day || g_decisions.empty()) {
        return;
    }
    if (g_first_day < 0) {
        g_first_day = day;
        g_first_qpc = qpc();
        g_period_start_day = day;
    } else {
        if (day != g_last_day + 1) {
            mark_all_dirty();  // the date jumped: a save was loaded, flags may be anything now
        }
        if (day - g_period_start_day >= kReportEveryDays) {
            if (g_profiling) {
                write_report(g_last_day);
            }
            if (g_cull) {
                log_cull_summary(g_last_day);
                log_parallel_summary(g_last_day);
            }
            g_period_start_day = day;
        }
    }
    g_last_day = day;
}

bool hook_slot(uint32_t slot_rva, uint32_t expected_rva, uintptr_t replacement, uintptr_t* original, const char* what) {
    uintptr_t slot = hooks::exe_addr(slot_rva);
    *original = read32(slot);
    if (*original != hooks::exe_addr(expected_rva) || !hooks::replace_pointer(slot, *original, replacement)) {
        logf("decision_cull: %s slot not recognised, feature off", what);
        return false;
    }
    return true;
}

}  // namespace

void install_decision_cull(const Config& config) {
    g_cull = config.cull_decision_checks;
    g_profiling = config.decision_profiling;
    g_parallel = config.parallel_decisions;
    if (g_parallel && !g_cull) {
        logf("parallel_decisions: needs cull_decision_checks = 1 (it extends the cull's per-country lists), off");
        g_parallel = 0;
    }
    if (g_parallel && g_profiling) {
        logf("parallel_decisions: off while decision_profiling is on (the timing hook is not thread-safe)");
        g_parallel = 0;
    }
    if (g_parallel && !config.fast_ai_queue) {
        logf("parallel_decisions: needs fast_ai_queue = 1 (the batch runs in the frame that drains the AI queue), off");
        g_parallel = 0;
    }
    g_decision_check_trivial = hooks::exe_addr(kDecisionCheckTrivialRva);
    if (g_parallel && !hooks::expect_bytes(g_decision_check_trivial, "\xB0\x01\xC3", 3)) {
        logf("parallel_decisions: CDecision slot 6 not recognised, off");
        g_parallel = 0;
    }
    if (!g_cull && !g_profiling) {
        logf("decision_cull: disabled");
        return;
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;
    g_report_path = std::wstring(host().game_dir) + L"\\v2cp_decisions.log";

    g_and_vtable = hooks::exe_addr(kAndVtableRva);
    g_not_vtable = hooks::exe_addr(kNotVtableRva);
    g_tag_vtable = hooks::exe_addr(kTagVtableRva);
    g_has_country_flag_vtable = hooks::exe_addr(kHasCountryFlagVtableRva);
    g_has_global_flag_vtable = hooks::exe_addr(kHasGlobalFlagVtableRva);
    g_is_ai_vtable = hooks::exe_addr(kIsAiVtableRva);
    g_scope_ctor = reinterpret_cast<ScopeCtorFn>(hooks::exe_addr(kScopeCtorRva));
    std::memcpy(g_fake_vtable, reinterpret_cast<const void*>(g_and_vtable), sizeof(g_fake_vtable));
    g_original_evaluate = reinterpret_cast<EvaluateFn>(g_fake_vtable[kVtableEvaluate / 4]);
    if (!hooks::expect_bytes(reinterpret_cast<uintptr_t>(g_original_evaluate), "\x55\x8B\xEC", 3) ||
        !hooks::expect_bytes(reinterpret_cast<uintptr_t>(g_scope_ctor), "\x55\x8B\xEC", 3)) {
        logf("decision_cull: CAndTrigger::Evaluate / CEventScope ctor not recognised, feature off");
        return;
    }
    g_fake_vtable[kVtableEvaluate / 4] = reinterpret_cast<uintptr_t>(&potential_hook);

    if (g_cull) {
        uintptr_t getter = hooks::exe_addr(kDecisionManagerGetterRva);
        for (uint32_t site : kGetterCallSites) {
            if (!hooks::expect_bytes(hooks::exe_addr(site), "\xE8", 1)) {
                logf("decision_cull: AI call site %08X not recognised, feature off", site + 0x400000);
                return;
            }
        }
        uintptr_t original = 0;
        if (!hook_slot(kClrCountryFlagSlotRva, kClrCountryFlagExecuteRva,
                       reinterpret_cast<uintptr_t>(&clr_country_flag_hook), &original, "CClrCountryFlagEffect::Execute")) {
            return;
        }
        g_original_clr_country_flag = reinterpret_cast<ExecuteFn>(original);
        if (!hook_slot(kClrGlobalFlagSlotRva, kClrGlobalFlagExecuteRva, reinterpret_cast<uintptr_t>(&clr_global_flag_hook),
                       &original, "CClrGlobalFlagEffect::Execute")) {
            return;  // the country-flag hook alone is harmless: it only marks lists dirty
        }
        g_original_clr_global_flag = reinterpret_cast<ExecuteFn>(original);
        uintptr_t ai_decisions = hooks::exe_addr(kAiDecisionsRva);
        if (!hooks::expect_bytes(hooks::exe_addr(kAiDecisionsCallSiteRva), "\xE8", 1) ||
            !hooks::expect_bytes(ai_decisions, "\x55\x8B\xEC\x6A\xFF", 5)) {
            logf("decision_cull: AI_DecisionUpdate call site not recognised, feature off");
            return;
        }
        for (uint32_t site : kGetterCallSites) {
            if (!hooks::redirect_call(hooks::exe_addr(site), getter, reinterpret_cast<uintptr_t>(&getter_stub))) {
                logf("decision_cull: AI call site %08X does not call DecisionManager_Get, feature off", site + 0x400000);
                return;  // an already redirected site keeps working on its own
            }
        }
        g_original_ai_decisions = reinterpret_cast<AiDecisionsFn>(ai_decisions);
        if (!hooks::redirect_call(hooks::exe_addr(kAiDecisionsCallSiteRva), ai_decisions,
                                  reinterpret_cast<uintptr_t>(&ai_decisions_hook))) {
            logf("decision_cull: AI_DecisionUpdate call site does not call it, feature off");
            return;
        }
    }
    if (!hooks::on_hourly_tick(&hourly_tick)) {
        logf("decision_cull: hourly tick hook unavailable, feature off");
        return;
    }
    logf("decision_cull: active (cull %s: per-country AI lists, rebuilt on flag clears and every %d days; profiling %s)",
         g_cull ? "on" : "off", kRefreshDays, g_profiling ? "on" : "off");
    if (g_parallel) {
        logf("parallel_decisions: active (%s): decision checks of the AI queue evaluated on worker threads at max speed",
             g_parallel == 2 ? "shadow mode, the game still uses its own evaluation" : "results used");
    }
}

void decisions_frame_begin(void* queue_ref) { frame_begin(queue_ref); }
void decisions_frame_end() { frame_end(); }

}  // namespace cp::features
