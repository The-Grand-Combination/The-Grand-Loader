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
| `demand_province` | A new `cb_types.txt` keyword for mods: `po_demand_province = yes` in a CB makes it target a state as `po_demand_state` does (the same war-goal choice, AI, infamy, `allowed_states` and texts), but the peace treaty takes only the war goal's province, and the war goal costs only that province's share of war score. The country is annexed only when that province is all it owns. Infamy is the `po_demand_state` define times the CB's `badboy_factor`, so set that factor for the smaller demand. Without the plugin the game logs the keyword as unknown and the CB has no peace option. Every player in a multiplayer game needs the plugin. |
| `event_background` | `background = "war"` in an event's body (next to `picture`) replaces the background of that event's window with `gfx/pictures/events/backgrounds/war.dds` (or `.tga`), found as the game finds a picture: in the mod first, then in the game. Nothing to declare. The sprite is stretched to the window's background, so size it for the window the event uses (country, province, `major` or `election`). Without the plugin the game logs the line as an unexpected key and shows the default background. Display only. |
| `vassal_map_color` | On the political map a substate or puppet is drawn in a blend of its overlord's colour and its own: `vassal_map_color_own` percent of its own (default 30; 0 = the overlord's colour, 100 = vanilla). Each vassal stays distinct while reading as a shade of its overlord; occupation stripes use the same shades. Uses the current colour of both countries, per-government colours included. Display only: the country's colour and saves are untouched. |

How it works, with the addresses: `docs/re/systems/map-modes.md` (map modes, political colours), `docs/re/systems/buildings.md` and `docs/re/systems/casus-belli.md` (`po_demand_province`).

## Building

Built with the rest of the repository (`build.cmd`); turn it off with `-DBUILD_SCRIPT_EXTENDER=OFF`.
