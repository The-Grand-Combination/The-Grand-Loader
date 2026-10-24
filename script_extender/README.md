# Victoria II Script Extender for TGC

A plugin for The Grand Loader that extends the game's script for The Grand Combination
(Victoria II 3.04). Separate from the Performance Patch: its own version, settings and release zip.

## Layout in the game folder

```
dinput8.dll                                The Grand Loader
mod/script_extender.ini                    settings (its presence enables the plugin)
mod/script_extender/script_extender.dll    the plugin
```

## Features

| ini key | what it does |
|---|---|
| `building_map_mode` | The infrastructure map mode shows one province building at a time. Clicking its button while the mode is shown moves to the next province building of `buildings.txt`, custom types included. Only the player's provinces are coloured (port buildings: only coastal ones): blue = as high as the current technology allows; blue stripes = can be expanded there; red stripes = anything else. A level under construction counts as built. The menubar tooltip reads "Infrastructure Map Mode - <building>". |

How it works, with the addresses: `docs/re/systems/map-modes.md` and `docs/re/systems/buildings.md`.

## Building

Built with the rest of the repository (`build.cmd`); turn it off with `-DBUILD_SCRIPT_EXTENDER=OFF`.
