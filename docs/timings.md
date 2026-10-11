# First-year timings

TGC, new game 1836, speed 5 (max), `engine_worker_threads = 4`, same machine, measured by `year_timer`
(active time of 1836 divided by 365; "1.11 min" is how `year_timer` prints 1 min 11 s). One run per row. The game is not deterministic between runs
(different wars, crises, AI states), so differences under ~5 ms/day need three runs per configuration
and a comparison of medians before they mean anything.

| configuration | first year | ms/day | vs. baseline |
|---|---|---|---|
| everything off | 1.11 min | 196 | |
| `cull_decision_checks` | 1.08 min | 189 | −7 ms/day (−3.6 %) |
| `cull_cb_checks` | 1.07 min | 185 | −11 ms/day (−5.6 %) |
| both | 1.08 min | 187 | −9 ms/day (−4.6 %); not additive, see noise above |
| `skip_network_wait = 1` (culls off, `engine_worker_threads = 0`, `sample_profiler = 1`) | **0.56 min** | **155** | **−40 to −48 ms/day (−20 to −22 %)** against 195–203 in the same conditions (2026-10-11) |
| **all**: `skip_network_wait = 1` + both culls (`engine_worker_threads = 8`, no profiler) | **0.52 min** | **144** | **−52 ms/day (−26 %)** against 196 (2026-10-11) |
| all + `prefetch_relations = 1` (`engine_worker_threads = 4`, no profiler) | **0.49 min** active | **137** | **−59 ms/day (−30 %)** against 196; −7 against 144 (2026-10-11) |
| all + `prefetch_relations` + `fast_ai_queue = 1` (`engine_worker_threads = 4`, no profiler) | **0.48 min** active | **133** | **−63 ms/day (−32 %)** against 196; −4 against 137, within noise (two runs: 133, 134; `cull_cb_checks` was silently off, see re/log.md; 2026-10-11) |
| all, everything really active: + `cull_cb_checks` (fixed) + `prefetch_pops = 1` (`engine_worker_threads = 4`, no profiler) | **0.48 min** active | **132** | **−64 ms/day (−33 %)** against 196; −1 to −2 against 133/134: the two together are within noise (one run, 2026-10-11) |

## Where the 196 ms/day go (1836, tick profiler)

| block | ms/day | share | patch |
|---|---|---|---|
| per-frame update outside the simulation | ~90 | 45 % | ~46 of it is the game thread blocked in the ENet network poll (15–35 ms timeout): `skip_network_wait`; render ~8 |
| world market (`Economy_Update`) | 37 | 19 % | none; internals not profiled yet (level 5) |
| per-country pre-pass (`FUN_00508590`) | 21 | 11 % | none; internals not profiled yet (level 5) |
| `Country_DailyUpdate` own work | ~20 | 10 % | none |
| decision `potential` evaluations | 23.5 | 12 % | `cull_decision_checks`: AI sees ~945 of 1651 decisions per country |
| AI CB scans | 11 | 6 % | `cull_cb_checks`: one scope pair per target, `can_use` only where it can pass |
| AI (rest) | ≤8, spiky | | none |
| events | 3 | | none |
| provinces | 2 | | none |

Totals do not add up exactly: the blocks come from different profiler levels and runs. History of the
builds and of the discarded approaches: [re/log.md](../re/log.md).
