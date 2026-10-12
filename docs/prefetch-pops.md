# `prefetch_pops`

**Default 1.** Prefetches pops before the daily loops that walk them. Same results.
Source: `patch/src/features/performance/pops_prefetch.cpp`. Research:
[re/systems/performance.md](re/systems/performance.md).

## Why

Pops are kept in linked lists, one per (province, pop type): head at `province+0x194 + type*16`, next pop
at `pop+0x27C`. Every pop is a separate allocation, and the next one is only known once the current one
has arrived from memory, so these loops are one dependent cache miss after another. The hottest sampled
instructions sit right after the first read of each pop.

## How

Before the loop, the patch walks **all** the relevant lists side by side (one cursor per list, all
advanced together) and prefetches every pop's ~0x280 bytes and the object at `pop+0x1D4`, so the misses
overlap:

| loop | call site | batch |
|---|---|---|
| `Market_UpdateCountryPops` (daily market, per country) | `0x0048468F` | all pops of the country |
| `DayTick`'s per-province pop pass (`FUN_004B9C60`) | `0x00685B09` | the next 8 provinces |
| market distribution (`FUN_0095EB00`, inline walk) | `0x00481860` | the next pop of the list |
| `Country_DailyUpdate` (monthly pop pass, reads every pop daily) | `0x00685ED1` | all pops of the country |
| `FUN_0050B9B0` in `Country_PrePass` | `0x0050861B` | all pops of the country (shared with [skip-clean-resets.md](skip-clean-resets.md)) |

## Result

Not measured on its own: part of the 133 → 132 → 124 ms/day steps of the first year (see
[timings.md](timings.md)). Expected to matter more later in the game, when there are more pops.
