#pragma once

#include <cstdint>

#include "config.h"

namespace cp::features {

// `prefetch_pops`: the daily market update walks every pop of a country through per-province, per-type
// linked lists, and the sampling profiler shows it stalled on the first read of each pop. Before each
// country, all its lists are walked side by side and every pop is prefetched, so the misses overlap
// instead of happening one after another. Read-only: same results. See docs/re/systems/performance.md.
void install_pops_prefetch(const Config& config);

// Prefetches every pop of `country` (all its pop lists walked together); nothing when prefetch_pops is off.
void prefetch_country_pops(uintptr_t country);

}  // namespace cp::features
