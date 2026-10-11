#pragma once

#include <string>

namespace cp {

struct Config {
    int engine_worker_threads = 0;  // 0 = leave the game default
    bool cull_cb_checks = false;     // skip per-defender can_use checks once an attacker-only condition failed
    bool cull_cb_profiling = false;  // count and time the CB checks, report every 30 game days
    int tick_profiler = 0;           // 0 off, 1 tick blocks + frame, 2 + DayTick calls, 3 + market/pre-pass, 4 + frame calls, 5 + internals
    bool cull_decision_checks = false;  // skip decision potentials that cannot pass (tag-locked, dead flag gate)
    bool decision_profiling = false;    // per-decision cost table in v2cp_decisions.log every 30 game days
    int sample_profiler = 0;            // 0 off, N = sample the game thread every N ms into v2cp_samples.tsv
    bool skip_network_wait = false;     // poll the network every frame without blocking (vanilla blocks 15-35 ms)
};

// Reads mod\community_patch.ini.
Config load_config(const std::wstring& config_file);

}  // namespace cp
