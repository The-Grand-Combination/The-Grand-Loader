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
`decision_profiling` is on.

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
  src/features/   one file per patch
  src/hooks/      hooking primitives
  mod/            the .ini shipped with the patch
docs/       research notes: Ghidra workflow, feature write-ups, the dated research log, timings.md (first-year table)
  raw/          unprocessed material: address book, keyword maps, trigger classification, call lists, decompilations, profiler logs
tools/ghidra/   headless Ghidra helper script used for the research
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

Addresses and behaviour come from the companion `Victoria2-Reverse-Engineering` repository; every
feature file names the function it hooks so the two can be cross-referenced. The research method
and a dated log of what was investigated live in `docs/`.
