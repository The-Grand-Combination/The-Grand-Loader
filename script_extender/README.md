# Victoria II Script Extender for TGC

A plugin for The Grand Loader that extends the game's script for The Grand Combination
(Victoria II 3.04). Separate from the Performance Patch: its own version, settings and release zip.

## Layout in the game folder

```
dinput8.dll                                The Grand Loader
mod/script_extender.ini                    settings (its presence enables the plugin)
mod/script_extender/script_extender.dll    the plugin
mod/script_extender/localisation/*.csv     its texts, in the game's CSV format (a mod may override a key)
```

## Features

| ini key | what it does |
|---|---|
| `building_map_mode` | The infrastructure map mode shows one province building at a time. Clicking its button while the mode is shown moves to the next province building of `buildings.txt`, custom types included. Only the player's provinces are coloured (port buildings: only coastal ones): blue = as high as the current technology allows; blue stripes = can be expanded there; red stripes = anything else. A level under construction counts as built. The menubar tooltip reads `SE_MAPMODE_BUILDING_TITLE` (`$MODE$ - $BUILDING$`), e.g. "Infrastructure Map Mode - Railroad". |

| `country_upgrade` | Ctrl+Shift+click on a province building's Expand button expands it in every province of the player's country where the game allows it, while the treasury covers the running total (one province per state for `one_per_state` buildings). The Expand tooltip gets a line for it under vanilla's Ctrl+click line (localisation key `SE_PROVINCE_VIEW_BUILD_COUNTRY_TOOLTIP`). The clicked province's own button must be enabled, as for Ctrl+click. Multiplayer-safe: it posts the same commands as clicking each province. |

How it works, with the addresses: `docs/re/systems/map-modes.md` and `docs/re/systems/buildings.md`.

## Building

Built with the rest of the repository (`build.cmd`); turn it off with `-DBUILD_SCRIPT_EXTENDER=OFF`.
