#pragma once

#include "config.h"

namespace cp::features {

// `prefetch_relations`: three daily scans walk a country's relation objects with every other country
// (683 x 683 objects, each allocated on its own), and the sampling profiler shows them stalled on cache
// misses at the first field they read. Before each scan, every relation the scan will read is
// prefetched, so the misses overlap instead of happening one by one. The scans themselves are not
// changed, except that the relation purge is skipped for relations with nothing to purge, which is
// exactly what it would have found. See re/systems/performance.md.
void install_relations_prefetch(const Config& config);

}  // namespace cp::features
