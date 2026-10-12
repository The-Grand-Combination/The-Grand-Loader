# `decision_profiling`

## Source

`patch/src/features/performance/decision_cull.cpp`.

## Why

To know which decisions cost the most to evaluate, how often each one is evaluated and how often it
passes, so that culling and script changes can be aimed at the right ones.

## How

- The vtable pointer of every decision's `potential` (an embedded `CAndTrigger`) is swapped for a private
  copy whose `Evaluate` slot times the call (1 call in 16, `QueryPerformanceCounter`).
- Every 30 game days `performance_patch_decisions.log` next to `v2game.exe` is rewritten with running
  per-day averages since the decisions were loaded, one line per decision: ms/day, evaluations/day, ns per
  evaluation, passes/day, and flags `T` (tag-locked), `F` (flag gate), `A` (top-level `ai` check). With
  `cull_decision_checks` on, the header also says how many decisions the AI saw per country.
- The timing hook is not thread-safe, so `parallel_decisions` is off while it runs.
