# Profilers

Diagnostics of the Performance Patch. Leave them off when playing; use one at a time (the call-timing
profiler's hooks would show up in the samples). All write next to `v2game.exe`.

| option | what | output | page |
|---|---|---|---|
| `tick_profiler = 1..5` | times the blocks of the daily tick and the frame by redirecting call sites; levels add detail | table in `the_grand_loader.log` every 30 game days | [tick-profiler.md](tick-profiler.md) |
| `sample_profiler = N` | every N ms, records where the game thread is and the calls on its stack; sees virtual calls, other DLLs and the exact instruction waited on | `performance_patch_samples.tsv` every 30 game days, read with `docs/tools/scripts/annotate_samples.sh` | [sample-profiler.md](sample-profiler.md) |
| `decision_profiling = 1` | cost of every decision's `potential` (1 call in 16 timed) | `performance_patch_decisions.log` every 30 game days | [../cull-decision-checks.md#profiling](../cull-decision-checks.md#profiling) |
| `cull_cb_profiling = 1` | counts and times the AI's casus belli checks (needs `cull_cb_checks = 1`) | `the_grand_loader.log` every 30 game days | [../cull-cb-checks.md#log](../cull-cb-checks.md#log) |
| always on | one line per game year: wall time, active time (pauses over 2 s excluded) and ms per game day | `the_grand_loader.log` | |

`decision_profiling` turns `parallel_decisions` off while it runs (its timing hook is not thread-safe).
