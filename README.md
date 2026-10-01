# Victoria II Community Patches

Runtime patches for `v2game.exe` (3.04) delivered as ordinary Victoria II mods. Nothing in the game
installation is modified: one proxy DLL is dropped next to the executable once, and every patch lives in
the `mod/` folder like any other mod, enabled from the launcher like any other mod.

```
Victoria 2/
├── v2game.exe
├── dinput8.dll                      ← loader (drop once)
└── mod/
    ├── community_patch.mod          ← mod descriptor + settings
    └── community_patch/
        └── community_patch.dll      ← the patch
```

## Install

1. Copy `dinput8.dll` next to `v2game.exe`.
2. Copy `mod/community_patch.mod` and `mod/community_patch/` into the game's `mod/` folder.
3. Enable **Community Patches** in the launcher, together with whatever mods you play.

`v2cp.log` is written next to `v2game.exe` on every launch and lists what was loaded and applied.

## Settings

Settings are plain keys inside `mod/community_patch.mod`; the game ignores keys it does not know.

| Key                     | Default | Effect                                                                  |
| ----------------------- | ------- | ----------------------------------------------------------------------- |
| `engine_worker_threads` | `4`     | Thread count handed to the engine's TBB scheduler (1-64). `0` = vanilla. |

## How it works

`dinput8.dll` is a transparent proxy for the system DirectInput 8 library, which `v2game.exe` imports
statically, so it is mapped before any game code runs. On load it reads every `-mod=` argument the
launcher passed, opens each `.mod`, and if `<path>/<modname>.dll` exists it loads it and calls its
`V2CP_PluginInit` export with a [`V2CP_HostApi`](launcher/include/v2cp/plugin_api.h). Any mod can ship
a plugin this way; `community_patch` is simply the first one.

## Repository layout

```
launcher/   dinput8.dll — proxy, mod discovery, plugin loading, log. Owns the plugin ABI.
patch/      community_patch.dll — the Community Patches plugin.
  src/features/   one file per patch
  src/hooks/      hooking primitives
  mod/            the .mod descriptor shipped with the patch
```

## Build

Requires the MSVC x86 toolchain, CMake ≥ 3.25 and Ninja (all included in Visual Studio Build Tools
with the "Desktop development with C++" workload).

```
# from an "x86 Native Tools Command Prompt"
cmake --preset x86-release
cmake --build --preset x86-release    # compiles and lays out build/ exactly as it goes into the game folder
```

Intermediates go to `out/`; `build/` only ever contains the drop-in layout (`dinput8.dll` + `mod/`).
CI builds every push and attaches `build/` as an artifact; tags matching `v*` publish a release zip.

## Reverse-engineering notes

Addresses and behaviour come from the companion `Victoria2-Reverse-Engineering` repository; every
feature file names the function it hooks so the two can be cross-referenced.
