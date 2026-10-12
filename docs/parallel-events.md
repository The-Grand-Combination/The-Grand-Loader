# `parallel_events`

**Default 1.** Evaluates the daily event scan's triggers on worker threads. `2` = shadow mode (compare
only). Source: `patch/src/features/performance/events_parallel.cpp`. Research:
[re/systems/parallel-triggers.md](re/systems/parallel-triggers.md), [re/systems/events.md](re/systems/events.md).

## Why

Every day `EventManager_DailyScan` (`0x008A7CD0`) evaluates the `trigger` of every candidate event of every
country (odd days) or owned province (even days); on true, it computes the MTTH, draws a random number and
may fire the event. Triggers only read the game; firing an event runs its effects at once and changes it.

## How

- Before the scan, the patch lists, in the scan's exact order, every (trigger, country or province) pair
  it will evaluate, and points those trigger blocks at a private copy of their vtable.
- When the scan asks for a trigger, the next chunk of 1024 entries is evaluated on the worker pool with
  copies of the scan's own scope (pointers inside the scope, such as THIS at `+0x24`, moved into each
  copy), and each call is answered from it as long as it is exactly the next entry. Anything else runs
  the original code.
- When an event fires, the results computed before it are dropped and recomputed (at most one chunk is
  wasted per fired event). The MTTH, the random draw and the firing stay on the game thread, in order.
- `2` (shadow): the game keeps its own evaluation; the parallel results are compared and mismatches
  logged.

## Result

Shadow mode: **0 mismatches in 2.9 M triggers** (after a fix to the scope copy; the first version
disagreed on ~0.7 %). ~5 000 triggers a day, ~0.5 ms on the game thread. Together with
`parallel_decisions` and `prefetch_ai_relations`: 132 → **124 ms/day** in the first year.
