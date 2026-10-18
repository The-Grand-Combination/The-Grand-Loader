#include <cstdint>
#include <cstring>
#include <vector>

#include "features/performance/decision_cull.h"
#include "features/performance/decision_cull_internal.h"
#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"
#include "parallel/worker_pool.h"
#include "util/clock.h"

// parallel_decisions (docs/parallel-decisions.md, docs/re/systems/decisions.md):
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

namespace cp::features::decisions {

namespace {

// --- v2game.exe 3.04 (RVA = VA - 0x00400000) -------------------------------------------------------
constexpr uint32_t kAiObjectTag = 0x30;                  // AI_DecisionUpdate's object: {tag, index} at +0x30
constexpr uint32_t kDecisionCheckSlot = 0x18;            // CDecision slot 6
constexpr uint32_t kDecisionCheckTrivialRva = 0x631490;  // MOV AL,1 / RET
constexpr uintptr_t kAiScopeParent = 0x2D;               // AI_DecisionUpdate: PUSH 0x2D at 0x008211B3
constexpr uint32_t kQueueNodeCountry = 0x04;  // AI job node: +0 tag, +4 country index, +8 prev, +0xC next
constexpr uint32_t kQueueNodeNext = 0x0C;
constexpr int kMaxMismatchLogs = 20;

using game::read32;

int g_parallel = 0;  // 0 off, 1 on, 2 shadow
uintptr_t g_decision_check_trivial = 0;

struct Precomputed {
    std::vector<uintptr_t> a;  // decisions of the country's list A whose checks passed, in list order
    std::vector<uintptr_t> b;
    uintptr_t ai = 0;
    uint32_t lists_version = 0;
    uint32_t frame = 0;   // frame of the computation; 0 = never
    uint32_t queued = 0;  // frame whose batch includes this country
    bool ok = false;      // false when a decision had an unexpected slot 6
};
std::vector<Precomputed> g_pre;
std::vector<uint32_t> g_pending;  // countries of the current frame's batch
uint32_t g_frame = 0;
bool g_frame_open = false;

// Statistics since load.
struct ParallelStats {
    uint64_t frames = 0;
    uint64_t countries = 0;
    uint64_t ticks = 0;    // wall time of the batches on the game thread
    uint64_t checked = 0;  // decisions evaluated by the batches
    uint64_t passed = 0;
    uint64_t used = 0;              // AI updates served from a batch
    uint64_t shadow_compared = 0;   // decisions compared in shadow mode
    uint64_t shadow_mismatches = 0;
};
ParallelStats g_stats;
int g_mismatch_logs = 0;

void make_ai_scope(unsigned char* scope, uintptr_t ai) {
    std::memset(scope, 0, game::kScopeBuffer);
    g_scope_ctor(scope, nullptr, reinterpret_cast<void*>(kAiScopeParent));
    std::memcpy(scope + game::kScopeTag, reinterpret_cast<const void*>(ai + kAiObjectTag), 8);  // {tag, index}
}

// 1 = passes, 0 = fails, -1 = slot 6 is not the expected `return true`.
int decision_passes(uintptr_t decision, void* scope) {
    if (read32(read32(decision) + kDecisionCheckSlot) != g_decision_check_trivial) {
        return -1;
    }
    bool passes = game::evaluate(decision + kDecisionPotential, scope) &&
                  game::evaluate(decision + kDecisionAllow, scope);
    return passes ? 1 : 0;
}

// The decisions of `in` that pass, appended to `out`; false at an unexpected slot 6.
bool filter_list(const std::vector<uintptr_t>& in, std::vector<uintptr_t>& out, void* scope) {
    for (uintptr_t decision : in) {
        int r = decision_passes(decision, scope);
        if (r < 0) {
            return false;
        }
        if (r) {
            out.push_back(decision);
        }
    }
    return true;
}

// Worker task: one country. Reads the game and g_lists; writes only its own Precomputed, whose vectors
// were reserved by the game thread, so nothing is allocated here.
void precompute_country(int index, void*) {
    uint32_t country = g_pending[static_cast<size_t>(index)];
    Precomputed& p = g_pre[country];
    const CountryLists& lists = g_lists[country];
    alignas(16) unsigned char scope[game::kScopeBuffer];
    make_ai_scope(scope, p.ai);
    p.ok = filter_list(lists.a, p.a, scope) && filter_list(lists.b, p.b, scope);
    p.frame = g_frame;
}

// Adds `country` to this frame's batch when it runs its AI here and is not in the batch yet; returns
// the number of decisions it will check.
size_t enqueue_country(uint32_t country, int32_t day) {
    uintptr_t object = country < g_lists.size() ? game::country_or_null(country) : 0;
    uintptr_t ai = object ? read32(object + game::kCountryAiObject) : 0;
    if (!ai || g_pre[country].queued == g_frame) {
        return 0;
    }
    CountryLists& lists = g_lists[country];
    if (lists_stale(lists, country, day)) {
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
    return lists.a.size() + lists.b.size();
}

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

void log_mismatch(uint32_t country, uintptr_t decision, bool serial, bool in_batch) {
    if (g_mismatch_logs++ >= kMaxMismatchLogs) {
        return;
    }
    uintptr_t object = game::country_or_null(country);
    char tag[4] = {};
    if (object) {
        std::memcpy(tag, reinterpret_cast<const void*>(object + game::kCountryTag), 3);
    }
    logf("parallel_decisions: MISMATCH day %d %s (%u) %s: serial %d, parallel %d", game::current_day(), tag, country,
         decision_name(decision).c_str(), serial ? 1 : 0, in_batch ? 1 : 0);
}

// Shadow mode: evaluates one list here, on the game thread, right before the game does, and compares
// with the batch, which holds the passing decisions of `in` in the same order.
void shadow_compare_list(uint32_t country, const std::vector<uintptr_t>& in, const std::vector<uintptr_t>& batch,
                         void* scope) {
    size_t at = 0;
    for (uintptr_t decision : in) {
        bool in_batch = at < batch.size() && batch[at] == decision;
        if (in_batch) {
            at++;
        }
        bool serial = decision_passes(decision, scope) == 1;
        g_stats.shadow_compared++;
        if (serial != in_batch) {
            g_stats.shadow_mismatches++;
            log_mismatch(country, decision, serial, in_batch);
        }
    }
}

void shadow_compare(uint32_t country, uintptr_t ai, const Precomputed& p) {
    alignas(16) unsigned char scope[game::kScopeBuffer];
    make_ai_scope(scope, ai);
    shadow_compare_list(country, g_lists[country].a, p.a, scope);
    shadow_compare_list(country, g_lists[country].b, p.b, scope);
}

}  // namespace

int resolve_parallel_mode(const Config& config) {
    g_decision_check_trivial = hooks::exe_addr(kDecisionCheckTrivialRva);
    g_parallel = 0;
    if (!config.parallel_decisions) {
        return 0;
    }
    if (!config.cull_decision_checks) {
        logf("parallel_decisions: needs cull_decision_checks = 1 (it extends the cull's per-country lists), off");
    } else if (config.decision_profiling) {
        logf("parallel_decisions: off while decision_profiling is on (the timing hook is not thread-safe)");
    } else if (!config.fast_ai_queue) {
        logf("parallel_decisions: needs fast_ai_queue = 1 (the batch runs in the frame that drains the AI queue), off");
    } else if (!hooks::expect_bytes(g_decision_check_trivial, "\xB0\x01\xC3", 3)) {
        logf("parallel_decisions: CDecision slot 6 not recognised, off");
    } else {
        g_parallel = config.parallel_decisions;
    }
    return g_parallel;
}

bool parallel_enabled() { return g_parallel != 0; }

void parallel_reset() {
    g_stats = {};
    g_pre.clear();
}

void parallel_serve(uint32_t country, uintptr_t ai) {
    const Precomputed* p = g_parallel ? precomputed_for(country, ai) : nullptr;
    if (!p) {
        return;
    }
    if (g_parallel == 1) {
        set_fake_list(kListA, p->a);
        set_fake_list(kListB, p->b);
        // manager_for_country counted the full lists
        g_cull_stats.ai_decisions_seen -= g_lists[country].a.size() + g_lists[country].b.size();
        g_cull_stats.ai_decisions_seen += p->a.size() + p->b.size();
    } else {
        shadow_compare(country, ai, *p);
    }
    g_stats.used++;
}

void log_parallel_summary(int32_t last_day) {
    int32_t days = g_window.days_until(last_day);
    if (!g_parallel || days <= 0 || g_stats.frames == 0) {
        return;
    }
    auto ull = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    double frames = static_cast<double>(g_stats.frames);
    logf("parallel_decisions: days %d-%d: %llu batches, %.0f countries and %.0f checks per batch, %.2f ms/day on the "
         "game thread (%d workers + game thread), %llu passed/day, %llu AI updates served",
         g_window.first_day, last_day, ull(g_stats.frames), static_cast<double>(g_stats.countries) / frames,
         static_cast<double>(g_stats.checked) / frames, ms(g_stats.ticks) / days, parallel::workers(),
         ull(g_stats.passed / days), ull(g_stats.used));
    if (g_parallel == 2) {
        logf("parallel_decisions: shadow: %llu checks compared, %llu mismatches", ull(g_stats.shadow_compared),
             ull(g_stats.shadow_mismatches));
    }
}

void parallel_frame_begin(void* queue_ref) {
    if (!g_parallel || g_decisions.empty() || !queue_ref) {
        return;
    }
    g_frame++;
    g_frame_open = true;
    uintptr_t bucket = read32(reinterpret_cast<uintptr_t>(queue_ref));
    int32_t day = game::current_day();
    if (!bucket || day < 0) {
        return;
    }
    if (g_pre.size() != g_lists.size()) {
        g_pre.assign(g_lists.size(), Precomputed{});
    }
    int64_t start = util::qpc();
    g_pending.clear();
    uint64_t checks = 0;
    // Bucket 0 is the one AI_FrameUpdate drains (the only one in single player).
    for (uintptr_t node = read32(bucket); node; node = read32(node + kQueueNodeNext)) {
        checks += enqueue_country(read32(node + kQueueNodeCountry), day);
    }
    if (g_pending.empty()) {
        return;
    }
    parallel::run(static_cast<int>(g_pending.size()), &precompute_country, nullptr);
    for (uint32_t country : g_pending) {
        g_stats.passed += g_pre[country].a.size() + g_pre[country].b.size();
    }
    g_stats.frames++;
    g_stats.countries += g_pending.size();
    g_stats.checked += checks;
    g_stats.ticks += static_cast<uint64_t>(util::qpc() - start);
}

void parallel_frame_end() { g_frame_open = false; }

}  // namespace cp::features::decisions

namespace cp::features {

void decisions_frame_begin(void* queue_ref) { decisions::parallel_frame_begin(queue_ref); }
void decisions_frame_end() { decisions::parallel_frame_end(); }

}  // namespace cp::features
