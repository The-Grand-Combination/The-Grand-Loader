# `cull_cb_profiling`

## Source

`patch/src/features/performance/cb_cull.cpp`.

## Why

To see what `cull_cb_checks` saves: how many casus belli checks the AI scans make and skip, what they
cost, and which CB types benefit most.

## How

- Requires `cull_cb_checks = 1`. The replaced AI scan loop counts its work and times it with a sampled
  clock (1 call in 64).
- Every 30 game days `the_grand_loader.log` gets a summary: wall time per game day, time inside the AI CB
  scans, type checks made and skipped per day, the cost of the once-a-day attacker pre-pass, and one line
  per CB type with the checks it skipped.
- With profiling off the loop keeps no counters.
