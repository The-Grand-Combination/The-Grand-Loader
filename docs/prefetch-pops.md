# `prefetch_pops`

## Source

`patch/src/features/performance/pops_prefetch.cpp`. Research: [re/systems/performance.md](re/systems/performance.md).

## Why

Pops are kept in linked lists, one per (province, pop type): head at `province+0x194 + type*16`, next pop
at `pop+0x27C`. Every pop is a separate allocation and the next one is only known once the current one has
arrived from memory, so these loops are one dependent cache miss after another. The hottest sampled
instructions sit right after the first read of each pop.

## How

Before the loop, all the relevant lists are walked side by side (one cursor per list, all advanced
together) and every pop's ~0x280 bytes, plus the object at `pop+0x1D4`, are prefetched, so the misses
overlap:

| loop | call site | batch |
|---|---|---|
| `Market_UpdateCountryPops` (daily market, per country) | `0x0048468F` | all pops of the country |
| `DayTick`'s per-province pop pass (`FUN_004B9C60`) | `0x00685B09` | the next 8 provinces |
| market distribution (`FUN_0095EB00`, inline walk) | `0x00481860` | the next pop of the list |
| `Country_DailyUpdate` (monthly pop pass, reads every pop daily) | `0x00685ED1` | all pops of the country |
| `FUN_0050B9B0` in `Country_PrePass` (thunk shared with `skip_clean_resets`) | `0x0050861B` | all pops of the country |
