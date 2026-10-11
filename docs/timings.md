# First-year timings

TGC, new game 1836, speed 5 (max), `engine_worker_threads = 4`, same machine, measured by `year_timer`
(active time of 1836 divided by 365). One run per row. The game is not deterministic between runs
(different wars, crises, AI states), so differences under ~5 ms/day need three runs per configuration
and a comparison of medians before they mean anything.

| configuration | first year | ms/day | vs. baseline |
|---|---|---|---|
| everything off | 1.11 min | 196 | |
| `cull_decision_checks` | 1.08 min | 189 | −7 ms/day (−3.6 %) |
| `cull_cb_checks` | 1.07 min | 185 | −11 ms/day (−5.6 %) |
| both | 1.08 min | 187 | −9 ms/day (−4.6 %); not additive, see noise above |

## Where the 196 ms/day go (1836, tick profiler)

| block | ms/day | share | patch |
|---|---|---|---|
| per-frame update outside the simulation (UI, render, wait) | ~90 | 45 % | none; internals not profiled yet (level 4) |
| world market (`Economy_Update`) | 37 | 19 % | none; internals not profiled yet (level 5) |
| per-country pre-pass (`FUN_00508590`) | 21 | 11 % | none; internals not profiled yet (level 5) |
| `Country_DailyUpdate` own work | ~20 | 10 % | none |
| decision `potential` evaluations | 23.5 | 12 % | `cull_decision_checks`: AI sees ~945 of 1651 decisions per country |
| AI CB scans | 11 | 6 % | `cull_cb_checks`: one scope pair per target, `can_use` only where it can pass |
| AI (rest) | ≤8, spiky | | none |
| events | 3 | | none |
| provinces | 2 | | none |

Totals do not add up exactly: the blocks come from different profiler levels and runs. History of the
builds and of the discarded approaches: [research-log.md](research-log.md).
