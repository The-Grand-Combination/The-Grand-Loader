#pragma once

#include "config.h"

namespace cp::features {

// Statistical profiler: a background thread suspends the game thread every `sample_profiler` ms while
// the game date is advancing, records where it is (EIP) and the return addresses on its stack, and
// rewrites v2cp_samples.tsv every 30 game days with ms/day per address. Sees virtual calls, render and
// other modules, which call redirection (tick_profiler) cannot. Map the addresses to functions with
// tools/scripts/annotate_samples.sh. Diagnostic.
void install_sample_profiler(const Config& config);

}  // namespace cp::features
