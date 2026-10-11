#pragma once

#include "config.h"

namespace cp::features {

// Times the building blocks of the daily tick (world update, province/country loops, AI, events) and
// writes a per-block ms/day table to the log every 30 game days. Diagnostic only; off by default.
void install_tick_profiler(const Config& config);

// Crash-log hook: logs the hooked functions that were active when the process faulted.
void tick_profiler_crash_context();

}  // namespace cp::features
