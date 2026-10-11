# Next steps (performance)

Where the speed work goes after the easy cache-miss fixes (written 2026-10-11, at 132 ms/day in the
first year of TGC against 196 vanilla). Evidence and the budget: [systems/performance.md](systems/performance.md).

## Pending measurements

- `year_timer` and a `sample_profiler = 1` run with the current settings (`prefetch_pops` with both
  sites, `prefetch_ai_relations`, `+0x2C` in `prefetch_relations`), against 132 ms/day. The last sample
  file is from the run with culls and prefetches off, so its ranking is stale.
- `fast_ai_queue`: a second run before shipping it on.
- `skip_network_wait` in multiplayer.

## Options, in recommended order

1. **Profile the late game (cheap, high information).** Every budget so far is 1836. Players complain
   about 1880+, with 2-3x the pops, wars, units and pathfinding. One `sample_profiler = 1` run on a
   ~1880-1900 save, then redo the hot-spot table.
2. **Drop dead countries from the country x country loops (cheap).** `AI_TargetScan`, the
   `Country_PrePass` loop and `Country_Recalculate` walk all 683 tags; in 1836 most have no provinces
   and each still costs a relation cache miss. Skip a dead tag only where its branch is provably a no-op
   (checked in code, as for the relation purge). Gain: part of the ~20 ms/day these scans still cost.
3. **Throttle rendering at max speed (medium).** `Render_MapFrame` is ~15 ms/day; at speed 5 nobody
   needs 30+ frames per game day. Render at most N fps at speed 5 only. Trade-off: the map updates
   less often while it runs.
4. **Parallelise read-only phases (highest potential, highest effort; first step built: `parallel_decisions`).** Full parallel simulation
   needs bit-identical sums or multiplayer desyncs. Viable path: run only the reading parts in parallel
   (decision `potential`s, AI target scans) for all countries and let the original loop consume the
   results. Feasibility study: [systems/parallel-triggers.md](systems/parallel-triggers.md).
5. **Contiguous allocation of pops and relations (research, invasive).** The root of every cache miss
   is one allocation per pop and per relation. A pool for those object sizes, installed before the
   game loads, would put a province's pops side by side and help every loop, not just the prefetched
   ones. Touches the game's allocator; save/load needs care.

Left open in the hot-spot scan: `FUN_0095EB00` / `FUN_0095A5F0` (market pop loops), `FUN_0052FCD0`,
the `has_country_flag` trie.
