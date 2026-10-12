#pragma once

#include "config.h"

namespace cp::features {

// `prefetch_pops`: the daily market update walks every pop of a country through per-province, per-type
// linked lists, and the sampling profiler shows it stalled on the first read of each pop. Before each
// country, all its lists are walked side by side and every pop is prefetched, so the misses overlap
// instead of happening one after another. Read-only: same results. See docs/re/systems/performance.md.
void install_pops_prefetch(const Config& config);

}  // namespace cp::features
