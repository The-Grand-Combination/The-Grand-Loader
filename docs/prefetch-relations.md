# `prefetch_relations`

**Default 1.** Prefetches country-pair data before the daily scans that read it. Same results.
Source: `patch/src/features/performance/relations_prefetch.cpp`. Research:
[re/systems/performance.md](re/systems/performance.md).

## Why

Every country has a separately allocated relation object for every other tag (`country+0xBE8[other]`,
~683 × 683 ≈ 466 000 objects), and several daily loops walk "this country × every other country",
reading one or two fields of each. The objects are scattered over the heap, so each read waits on RAM:
the sampled instruction right after the first read holds 70–90 % of these functions' time.

## How

Register-preserving thunks on the call sites issue `prefetch` instructions for the fields the scan is
about to read, for every country it will visit, so the cache misses overlap instead of happening one
after another:

| scan | call site | fields |
|---|---|---|
| `Country_Recalculate` (from `Country_PrePass`) | `0x005089DB` | `+0x08 +0x0C +0x20 +0x24 +0x28 +0x2C +0x34 +0x38 +0x70` |
| market capacity `FUN_00522CF0` (from `Market_AddRealDemand`) | `0x00487483` | `+0x34` |
| `CountryRelation_DailyPurge` (from `Country_DailyUpdate`) | `0x0050AC44` | `+0x20 +0x50 +0xA8` |
| `FUN_0052FCD0` (from `Country_DailyUpdate`) | `0x0050B6B9` | `other+0x1580[self]` (two passes) |

The purge thunk also skips the purge of a relation whose six top-level fields (`+0x50 +0xA8 +0x2C +0x30
+0x28 +0x20`) are all zero: the purge would do nothing for it. The country lists (with provinces, without,
all) are built once per game day.

A thunk at the start of `Country_PrePass` (shared with `skip_clean_resets`) also prefetches relation
`+0x2C/+0x30` for tags **without** provinces, since that loop runs for every tag (see
[skip-clean-resets.md](skip-clean-resets.md)).

## Result

First year (1836): 144 → **137 ms/day**. Prefetching only changes which memory is already in the cache.
