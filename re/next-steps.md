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
2. **~~Drop dead countries from the country x country loops~~ (checked 2026-10-11: three of the five scans
   already skip them; the other two have data-dependent bodies that write other countries, so a skip
   cannot be proven a no-op. Only a prefetch tweak came out of it.)**
3. **Throttle rendering at max speed (medium).** `Render_MapFrame` is ~15 ms/day; at speed 5 nobody
   needs 30+ frames per game day. Render at most N fps at speed 5 only. Trade-off: the map updates
   less often while it runs.
4. **Parallelise read-only phases (highest potential, highest effort; built: `parallel_decisions`, `parallel_events`; rejected: `Country_Recalculate`, world market).** Full parallel simulation
   needs bit-identical sums or multiplayer desyncs. Viable path: run only the reading parts in parallel
   (decision `potential`s, AI target scans) for all countries and let the original loop consume the
   results. Feasibility study: [systems/parallel-triggers.md](systems/parallel-triggers.md).
5. **Contiguous allocation of pops and relations (research, invasive).** Sized 2026-10-11: the heap calls
   cost ~1.7 ms/day and the CRT heap is already a size-class allocator (Windows LFH), so swapping in
   mimalloc saves little. The real fix would be layout (objects in loop order), not a faster allocator.

Left open in the hot-spot scan: `FUN_0095EB00` / `FUN_0095A5F0` (market pop loops), `FUN_0052FCD0`,
the `has_country_flag` trie.
