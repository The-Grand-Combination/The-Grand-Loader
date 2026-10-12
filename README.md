# Victoria II Community Patches

Runtime patches for `v2game.exe` (3.04). Nothing in the game installation is modified: one proxy DLL is
dropped next to the executable once, and every patch lives in the `mod/` folder, side by side with
regular mods but invisible to the game's own mod list.

```
Victoria 2/
├── v2game.exe
├── dinput8.dll                      ← loader (drop once)
└── mod/
    ├── community_patch.ini          ← settings; its presence enables the patch
    └── community_patch/
        └── community_patch.dll      ← the patch
```

## Install

1. Copy `dinput8.dll` next to `v2game.exe`.
2. Copy `mod/community_patch.ini` and `mod/community_patch/` into the game's `mod/` folder.
3. Launch the game as usual, with any mods you like. To disable the patch, remove or rename the `.ini`.

`v2cp.log` is written next to `v2game.exe` on every launch and lists what was loaded and applied. It also
gets one line per game year with how long the year took (wall time, and with pauses over two seconds
excluded), so runs can be timed without a stopwatch. `v2cp_decisions.log` appears there too when
`decision_profiling` is on, and `v2cp_samples.tsv` when `sample_profiler` is on.

## Settings

`mod/community_patch.ini`, plain `key = value` lines; `;` starts a comment.

| Key                     | Default | Effect                                                                  |
| ----------------------- | ------- | ----------------------------------------------------------------------- |
| `engine_worker_threads` | `4`     | Thread count handed to the engine's TBB scheduler (1-64). `0` = vanilla. |
| `cull_cb_checks`        | `0`     | Skips the per-target `can_use` check of a CB type for the rest of the day once a condition that depends only on the attacker has failed. AI daily scans only. Off by default: measured gain is within noise (`can_use` is ~5 ms/day in TGC). See [docs/cb-cull.md](docs/cb-cull.md). |
| `cull_cb_profiling`     | `0`     | With `cull_cb_checks`, counts and times the CB checks and reports every 30 game days (see the Log section of [docs/cb-cull.md](docs/cb-cull.md)). Off unless investigating. |
| `tick_profiler`         | `0`     | Times the daily-tick blocks (world update, provinces, countries, AI, events) and logs a ms/day table every 30 game days. See [docs/tick-profiler.md](docs/tick-profiler.md). |
| `cull_decision_checks`  | `1`     | Gives the AI loop a per-country decision list without the decisions that cannot pass for that country (a top-level `tag = X` of another country, a top-level `ai = yes/no` it fails, a `NOT has_*_flag` gate already failed); rebuilt when a flag is cleared, on load and every 90 days. Same results, nothing per decision. See [docs/decision-cull.md](docs/decision-cull.md). |
| `decision_profiling`    | `0`     | Rewrites `v2cp_decisions.log` every 30 game days with running per-day averages for every decision (ms/day, evaluations, ns per evaluation, skipped). Constant size. Off unless investigating. |
| `sample_profiler`       | `0`     | Every N ms, records where the game thread is (and the calls on its stack) while the date advances; rewrites `v2cp_samples.tsv` every 30 game days. Sees virtual calls and rendering, which `tick_profiler` cannot. Use `1`, with `tick_profiler = 0`. See [docs/sample-profiler.md](docs/sample-profiler.md). |
| `skip_network_wait`     | `1`     | Polls the network every frame without blocking. Vanilla blocks 15–35 ms per frame waiting for packets, also in single player (a local loopback). First year 155 instead of ~199 ms/day (−22 %); with both culls, 144 ms/day (−26 %). CPU use still drops when paused. Not yet checked in multiplayer. See [re/systems/network.md](re/systems/network.md). |
| `prefetch_relations`    | `1`     | Prefetches the country-to-country relation objects before the three daily scans that read them (country recalculation, market capacity, relation purge) and skips purges with nothing to purge. Same results; first year 144 -> 137 ms/day. |
| `prefetch_pops`         | `0`     | Before two daily per-pop loops (each country's market update; DayTick's province pass, 8 provinces at a time), walks the pop lists side by side and prefetches every pop, so the cache misses overlap. Read-only, same results. Experimental, not measured yet. See [re/systems/performance.md](re/systems/performance.md). |
| `prefetch_ai_relations` | `0`     | Prefetches the relation data the AI's daily target scan reads (one scan per AI country over every country) before it runs. Read-only, same results. Experimental, not measured yet. See [re/systems/performance.md](re/systems/performance.md). |
| `fast_ai_queue`         | `0`     | At max speed, lets the daily AI job queue finish in the frame it started in instead of 20 ms per frame (cap 250 ms), so a day needs fewer frames. Same jobs, same order; other speeds unchanged. First year 137 -> 133 ms/day (one run, within noise). See [re/systems/tick.md](re/systems/tick.md). |
| `parallel_decisions`    | `0`     | With `cull_decision_checks` and `fast_ai_queue`: at max speed, evaluates the AI's daily decision checks (`potential`, `allow`) for all waiting countries on worker threads, and the game's loop only sees the decisions that passed, in order. `2` = shadow mode: the game is unchanged and the parallel results are compared and logged. Experimental. See [re/systems/parallel-triggers.md](re/systems/parallel-triggers.md). |
| `parallel_events`       | `0`     | Evaluates the daily event scan's `trigger`s on worker threads, in the scan's order; a fired event drops the results computed before it. MTTH, the random draw and the firing stay on the game thread. `2` = shadow mode (compare and log only). Experimental. See [re/systems/parallel-triggers.md](re/systems/parallel-triggers.md). |
| `compress_flag_cache`   | `0`     | Creates, saves and loads the per-government flag atlases as DXT1 DDS instead of uncompressed TGA: 4 MB instead of 32 MB per atlas in TGC. Delete the old `flagfiles*` caches once so they are rebuilt. See [re/systems/flags.md](re/systems/flags.md). |

## How it works

`dinput8.dll` is a transparent proxy for the system DirectInput 8 library, which `v2game.exe` imports
statically, so it is mapped before any game code runs. On load it scans `mod/*.ini`; for each
`mod/<name>.ini` it loads `mod/<name>/<name>.dll` and calls its `V2CP_PluginInit` export with a
[`V2CP_HostApi`](launcher/include/v2cp/plugin_api.h). The game never reads these files: it only
recognises `.mod` descriptors, so the patches never show up in its mod list and cannot be confused
with content mods. Any project can ship a plugin this way; `community_patch` is simply the first one.

## Repository layout

```
launcher/   dinput8.dll — proxy, plugin discovery and loading, log. Owns the plugin ABI.
patch/      community_patch.dll — the Community Patches plugin.
  src/features/   one file per patch, by kind:
    performance/  speed patches (culls, prefetches, network wait, AI queue, parallel decisions/events, engine threads)
    memory/       memory patches (flag_cache)
    profilers/    diagnostics (tick_profiler, sample_profiler, year_timer)
  src/parallel/   worker pool shared by the parallel features
  src/hooks/      hooking primitives
  mod/            the .ini shipped with the patch
docs/       one page per shipped feature, timings.md (first-year table)
re/         reverse engineering of v2game.exe 3.04 — start at re/README.md
  symbols/      functions, globals, struct offsets and patch sites as CSV: the source of truth for addresses
  systems/      one page per game subsystem (tick, economy, events, CBs, decisions, ...)
  log.md        dated research log
  raw/          unprocessed material: keyword maps, trigger classification, call lists, decompilations, profiler logs
tools/ghidra/   headless Ghidra scripts: V2Dump (queries), V2Symbols (sync names with re/symbols)
tools/scripts/  check_patch_sites.sh (original bytes vs the exe), decision script analysis
```

## Build

Requires the MSVC x86 toolchain, CMake ≥ 3.25 and Ninja (all included in Visual Studio Build Tools
with the "Desktop development with C++" workload).

```
build.cmd                 # enters the x86 environment, configures and builds
deploy.cmd                # build.cmd + copies build/ into the game folder (V2_GAME_DIR)
```

`deploy.cmd` reads the game folder from `V2_GAME_DIR`; put `set "V2_GAME_DIR=<path>"` in a git-ignored
`deploy.local.cmd` next to it.

or, from an "x86 Native Tools Command Prompt":

```
cmake --preset x86-release
cmake --build --preset x86-release    # compiles and lays out build/ exactly as it goes into the game folder
```

Intermediates go to `out/`; `build/` only ever contains the drop-in layout (`dinput8.dll` + `mod/`).
CI builds every push and attaches `build/` as an artifact; tags matching `v*` publish a release zip.

## Reverse-engineering notes

Everything about the game binary lives in [re/](re/README.md): addresses as CSV, one page per subsystem,
the research log and the Ghidra workflow. Every feature file names the functions it hooks by the names
used there. Part of it was rebuilt from the notes in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering),
credited on each page that uses them.
