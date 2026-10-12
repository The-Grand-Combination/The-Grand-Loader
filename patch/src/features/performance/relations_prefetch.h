#pragma once

#include <cstdint>

#include "config.h"

namespace cp::features {

// `prefetch_relations`: three daily scans walk a country's relation objects with every other country
// (683 x 683 objects, each allocated on its own), and the sampling profiler shows them stalled on cache
// misses at the first field they read. Before each scan, every relation the scan will read is
// prefetched, so the misses overlap instead of happening one by one. The scans themselves are not
// changed, except that the relation purge is skipped for relations with nothing to purge, which is
// exactly what it would have found. See docs/re/systems/performance.md.
void install_relations_prefetch(const Config& config);

// `prefetch_ai_relations`: the same for the AI's daily target scan (FUN_0084B0F0, one per AI country),
// which reads relation +0x34 for every country. Read-only.
void install_ai_relations_prefetch(const Config& config);

// Prefetches relation `offset` of `country` with every tag 1..count-1 (per-day list); nothing when
// prefetch_relations is off. For loops that visit dead tags too (Country_PrePass at 0x00508DA7).
void prefetch_all_relations(uintptr_t country, uint32_t offset);

}  // namespace cp::features
