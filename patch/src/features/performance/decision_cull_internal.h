#pragma once

// Shared between decision_cull.cpp (the per-country lists), decision_parallel.cpp (parallel_decisions)
// and decision_profiling.cpp (decision_profiling). Not included anywhere else.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "game/triggers.h"

namespace cp::features::decisions {

// --- v2game.exe 3.04 ------------------------------------------------------------------------------
constexpr uint32_t kManagerSize = 0x38;        // DecisionManager_Get allocates 0x38 bytes
constexpr uint32_t kListA = 0x0C;              // vector<CDecision*>: begin +0, end +4, capacity +8
constexpr uint32_t kListB = 0x28;
constexpr uint32_t kDecisionName = 0x08;       // std::string (game::GameString)
constexpr uint32_t kDecisionPotential = 0x24;  // embedded CAndTrigger
constexpr uint32_t kDecisionAllow = 0x40;      // embedded CAndTrigger

constexpr int32_t kReportEveryDays = 30;  // report period of the cull, parallel and profiling summaries

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

// Per country: the two filtered lists the AI loop sees, and when they were built.
struct CountryLists {
    std::vector<uintptr_t> a;  // replaces manager list A
    std::vector<uintptr_t> b;  // replaces manager list B
    int32_t built_day = -1;
    int32_t controller = -1;  // world+0xAEC entry at build time: a change flips `ai = yes/no`
    uint32_t version = 0;     // bumped on every build; parallel results are only used for the version they saw
    bool dirty = true;
};

// Cull statistics since load (exact).
struct CullStats {
    uint64_t list_builds = 0;
    uint64_t list_build_ticks = 0;
    uint64_t removed_tag = 0;  // decisions left out of a list, summed over builds
    uint64_t removed_ai = 0;
    uint64_t removed_dead = 0;
    uint64_t ai_requests = 0;        // getter calls served with a filtered list
    uint64_t ai_decisions_seen = 0;  // list entries handed to the AI loop, summed over requests
};

// The days the summaries cover: from the first day seen after a load to the last one.
struct ReportWindow {
    int32_t first_day = -1;
    int64_t first_qpc = 0;
    int32_t last_day = -1;
    int32_t period_start_day = -1;

    int32_t days_until(int32_t last) const { return first_day < 0 ? 0 : last - first_day + 1; }
};

extern bool g_cull;
extern bool g_profiling;
extern game::ScopeCtorFn g_scope_ctor;
extern std::vector<DecisionInfo> g_decisions;
extern std::vector<CountryLists> g_lists;
extern CullStats g_cull_stats;
extern ReportWindow g_window;

// decision_cull.cpp
std::string decision_name(uintptr_t decision);
bool lists_stale(const CountryLists& lists, size_t country, int32_t day);
void build_lists(size_t country, int32_t day);
void set_fake_list(uint32_t offset, const std::vector<uintptr_t>& list);
double ms(uint64_t ticks);

// decision_parallel.cpp
int resolve_parallel_mode(const Config& config);  // 0 off, 1 on, 2 shadow; logs why it is off
bool parallel_enabled();
void parallel_reset();
// Called from the AI_DecisionUpdate hook with the country's copy of the manager in place: points it at
// the batch result (mode 1) or compares the batch with a serial evaluation (mode 2).
void parallel_serve(uint32_t country, uintptr_t ai);
void log_parallel_summary(int32_t last_day);
void parallel_frame_begin(void* queue_ref);
void parallel_frame_end();

// decision_profiling.cpp
bool profiling_install(uintptr_t and_vtable);  // false when CAndTrigger::Evaluate is not recognised
bool is_profiling_vtable(uintptr_t vtable);
void profiling_reset();
void profiling_attach();  // after the decisions were (re)read: points every potential at the timed vtable
void write_report(int32_t last_day);

}  // namespace cp::features::decisions
