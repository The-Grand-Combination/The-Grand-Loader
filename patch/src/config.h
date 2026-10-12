#pragma once

#include <string>

namespace cp {

struct Config {
    int engine_worker_threads = 0;  // 0 = leave the game default
    bool cull_cb_checks = false;     // skip per-defender can_use checks once an attacker-only condition failed
    bool cull_cb_profiling = false;  // count and time the CB checks, report every 30 game days
    int tick_profiler = 0;           // 0 off, 1 tick blocks + frame, 2 + DayTick calls, 3 + market/pre-pass, 4 + frame calls, 5 + internals
    bool cull_decision_checks = false;  // skip decision potentials that cannot pass (tag-locked, dead flag gate)
    bool decision_profiling = false;    // per-decision cost table in performance_patch_decisions.log every 30 game days
    int sample_profiler = 0;            // 0 off, N = sample the game thread every N ms into performance_patch_samples.tsv
    bool skip_network_wait = false;     // poll the network every frame without blocking (vanilla blocks 15-35 ms)
    bool prefetch_relations = false;    // prefetch relation objects before the three daily country x country scans
    bool prefetch_pops = false;         // prefetch each country's pops before its daily market update
    bool prefetch_ai_relations = false; // prefetch relations before the AI's daily target scan
    bool compress_flag_cache = false;   // create, cache and load the flag atlases as DXT1 instead of uncompressed
    bool fast_ai_queue = false;         // at max speed, finish the AI job queue in the frame it started in
    int parallel_decisions = 0;         // 0 off, 1 AI decision checks on worker threads, 2 shadow mode (compare only)
    int parallel_events = 0;            // 0 off, 1 event scan triggers on worker threads, 2 shadow mode (compare only)
};

// Reads mod\performance_patch.ini.
Config load_config(const std::wstring& config_file);

}  // namespace cp
