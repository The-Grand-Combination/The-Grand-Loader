#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "features/performance/decision_cull_internal.h"
#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"
#include "util/clock.h"

// decision_profiling (docs/decision-profiling.md): swaps the vtable pointer of every decision's
// `potential` (a CAndTrigger embedded at CDecision+0x24) for a private copy whose Evaluate slot times
// the call, and rewrites performance_patch_decisions.log with per-decision averages every report period.

namespace cp::features::decisions {

namespace {

constexpr size_t kVtableSlotsCopied = 16;  // CAndTrigger has 12 used slots; extra entries are never called
constexpr uint64_t kSample = 16;           // timing sample rate (power of two)

game::EvaluateFn g_original_evaluate = nullptr;
uintptr_t g_fake_vtable[kVtableSlotsCopied];
uintptr_t g_and_vtable = 0;
std::wstring g_report_path;

uint64_t g_total_calls = 0;  // exact: evaluations that went through the timed hook
uint64_t g_sample_counter = 0;

// potential address -> decision index; open addressing, usual probe length one.
struct LookupSlot {
    uintptr_t key = 0;
    uint32_t index = 0;
};
std::vector<LookupSlot> g_lookup;
uint32_t g_lookup_shift = 32;

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

DecisionInfo* lookup(uintptr_t key) {
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
    int64_t start = util::qpc();
    char result = g_original_evaluate(self, nullptr, scope);
    uint64_t ticks = static_cast<uint64_t>(util::qpc() - start);
    if (DecisionInfo* info = lookup(reinterpret_cast<uintptr_t>(self))) {
        info->ticks += ticks;
        info->calls++;
        info->passed += result ? 1 : 0;
    }
    return result;
}

unsigned long long ull(uint64_t v) { return static_cast<unsigned long long>(v); }

void write_header(FILE* out, int32_t last_day, int32_t days, double wall_ms_day, double eval_ms_day,
                  uint64_t passed) {
    size_t tag_locked = 0, gated = 0, ai_gated = 0;
    for (const DecisionInfo& d : g_decisions) {
        tag_locked += d.locked_country >= 0 ? 1 : 0;
        gated += d.gate ? 1 : 0;
        ai_gated += d.ai_child ? 1 : 0;
    }
    fprintf(out,
            "# decision profiling: running per-day averages since the decisions were loaded; rewritten every %d game "
            "days.\n"
            "# days %d-%d (%d days), wall %.1f ms/day, cull_decision_checks=%d\n"
            "# %llu potential evaluations/day costing %.2f ms/day (1 in %llu timed), %llu passed/day\n"
            "# %zu decisions: %zu tag-locked (T), %zu with a flag gate (F), %zu with a top-level ai check (A)\n",
            kReportEveryDays, g_window.first_day, last_day, days, wall_ms_day, g_cull ? 1 : 0,
            ull(g_total_calls / days), eval_ms_day, ull(kSample), ull(passed * kSample / days), g_decisions.size(),
            tag_locked, gated, ai_gated);
    const CullStats& c = g_cull_stats;
    if (g_cull && c.ai_requests && c.list_builds) {
        fprintf(out,
                "# cull: AI loop saw %.0f decisions per country on average; list builds left out %llu by tag, %llu by "
                "ai, %llu dead per build (%llu builds, %.2f ms/day)\n",
                static_cast<double>(c.ai_decisions_seen) / static_cast<double>(c.ai_requests),
                ull(c.removed_tag / c.list_builds), ull(c.removed_ai / c.list_builds),
                ull(c.removed_dead / c.list_builds), ull(c.list_builds), ms(c.list_build_ticks) / days);
    }
    fprintf(out, "#\n#   ms/day  evals/day  ns/eval  passed/day  TFA  decision\n");
}

void write_rows(FILE* out, int32_t days) {
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
}

}  // namespace

bool profiling_install(uintptr_t and_vtable) {
    g_and_vtable = and_vtable;
    g_report_path = std::wstring(host().game_dir) + L"\\performance_patch_decisions.log";
    std::memcpy(g_fake_vtable, reinterpret_cast<const void*>(and_vtable), sizeof(g_fake_vtable));
    constexpr size_t kEvaluateIndex = game::kTriggerEvaluateSlot / sizeof(uintptr_t);
    g_original_evaluate = reinterpret_cast<game::EvaluateFn>(g_fake_vtable[kEvaluateIndex]);
    if (!hooks::expect_bytes(reinterpret_cast<uintptr_t>(g_original_evaluate), "\x55\x8B\xEC", 3)) {
        return false;
    }
    g_fake_vtable[kEvaluateIndex] = reinterpret_cast<uintptr_t>(&potential_hook);
    return true;
}

bool is_profiling_vtable(uintptr_t vtable) { return vtable == reinterpret_cast<uintptr_t>(g_fake_vtable); }

void profiling_reset() { g_total_calls = 0; }

void profiling_attach() {
    if (!g_profiling) {
        return;
    }
    lookup_build();
    for (const DecisionInfo& d : g_decisions) {
        uintptr_t potential = d.decision + kDecisionPotential;
        if (game::read32(potential) == g_and_vtable) {
            *reinterpret_cast<uintptr_t*>(potential) = reinterpret_cast<uintptr_t>(g_fake_vtable);
        }
    }
}

void write_report(int32_t last_day) {
    int32_t days = g_window.days_until(last_day);
    if (!g_profiling || days <= 0 || g_decisions.empty()) {
        return;
    }
    uint64_t passed = 0, ticks = 0;
    for (const DecisionInfo& d : g_decisions) {
        passed += d.passed;
        ticks += d.ticks;
    }
    double wall_ms_day = ms(static_cast<uint64_t>(util::qpc() - g_window.first_qpc)) / days;
    double eval_ms_day = ms(ticks) * static_cast<double>(kSample) / days;
    logf("decision_profiling: days %d-%d (%d days, %.1f ms/day wall): %llu potential evaluations/day, %.2f ms/day, "
         "%llu passed/day -> performance_patch_decisions.log",
         g_window.first_day, last_day, days, wall_ms_day, ull(g_total_calls / days), eval_ms_day,
         ull(passed * kSample / days));

    FILE* out = nullptr;
    if (_wfopen_s(&out, g_report_path.c_str(), L"w") != 0 || !out) {
        logf("decision_profiling: cannot write performance_patch_decisions.log");
        return;
    }
    write_header(out, last_day, days, wall_ms_day, eval_ms_day, passed);
    write_rows(out, days);
    fclose(out);
}

}  // namespace cp::features::decisions
