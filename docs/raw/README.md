# docs/raw

Unprocessed material behind the write-ups in `docs/`. Nothing here is edited for reading; it is kept so
numbers and addresses can be re-checked without redoing the Ghidra or profiling runs.

| File | What it is |
|---|---|
| `addresses.md` | every address located so far: globals, country-object offsets, functions, vtables, patched call sites |
| `cb-keyword-map.txt` | `cb_types.txt` keyword -> token id -> `CCasusBelliType` offset; trigger block layout; scope layout |
| `decision-keyword-map.txt` | `political_decisions` keyword -> token id -> `CDecision` offset; where potential/allow are evaluated |
| `cb-callchains.txt` | every caller of `IsAnyCBUsable` and the chains that reach the daily / monthly tick |
| `trigger-classes.txt` | all 236 trigger classes classified from their `Evaluate` (scalar / country-ref / pair-dependent) |
| `trigger-vtables.txt` | the same classes with their vtable, Evaluate (slot 6), value parser (slot 9) and block parser (slot 4) addresses |
| `rtti-vftables.txt` | all 3111 RTTI vftable symbols Ghidra recovered |
| `calls-*.txt` | the direct `CALL` instructions inside each hot function (site, target, counts); source of the profiler site tables |
| `decomp/` | Ghidra decompilation of the functions the write-ups cite (raw, unrenamed) |
| `profiler/` | tick_profiler, cb_cull, decision_cache and decision_profiler log excerpts per run, with the measurement conditions in the header comment |
| `decisions/` | static analysis of the mod's decision scripts: once-only flag pattern, monotonic (dead-forever) conditions, first condition of every potential |

Conventions: addresses are VAs of `v2game.exe` 3.04 (image base 0x00400000); the profiler's `f_XXXXXXXX`
names are VAs of functions not yet named. Days in the logs are `date_hours / 24` without the year-0 base
subtracted, so they read as ~2495xxx.
