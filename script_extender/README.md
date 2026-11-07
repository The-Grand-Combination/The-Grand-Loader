# Victoria II Script Extender for TGC

A plugin for The Grand Loader that extends the game's script for The Grand Combination
(Victoria II 3.04). Separate from the Performance Patch: its own version, settings and release zip.

## Layout in the game folder

```
dinput8.dll                                The Grand Loader
mod/script_extender.mod                    its mod descriptor: load = "script_extender/script_extender.dll"
mod/script_extender/script_extender.dll    the plugin (a mod may declare it in its own .mod; loaded once)
mod/script_extender/script_extender.ini    settings
mod/script_extender/localisation/*.csv     its texts, in the game's CSV format (a mod may override a key)
```

## Features

| ini key | what it does |
|---|---|
| `building_map_mode` | The infrastructure map mode shows one province building at a time. Clicking its button while the mode is shown moves to the next province building of `buildings.txt`, custom types included. Only the player's provinces are coloured (port buildings: only coastal ones): blue = as high as the current technology allows; blue stripes = can be expanded there; red stripes = anything else. A level under construction counts as built. The menubar tooltip reads `SE_MAPMODE_BUILDING_TITLE` (`$MODE$ - $BUILDING$`), e.g. "Infrastructure Map Mode - Railroad". |
| `buildings_ai` | The AI builds the province buildings listed in the mod's `common/buildings_ai.txt` (the first `-mod=` folder that has it, else the game's; read on the first AI construction run). One block per building, with `chance` (rolled per run), `money` (the treasury must cover this factor of the cost) and `weight` (a `factor` / `base` block with `modifier`s and triggers, as a decision's `ai_will_do`, evaluated per owned province). Each run picks one province by weight among those where the building can be expanded and posts the same construction command the vanilla builders post, so every machine validates it; nothing is built at war, as in vanilla. The vanilla fort, naval base and railroad builders then find their building by name instead of file order, so a mod's extra type-4 building listed before the fort no longer takes the fort's place. The draws come from a generator of the plugin's own, seeded by the date, the country and the entry: the game's generators are left alone. With `buildings_ai_log = 1` (off by default) the log lists the file's entries when read, the first constructions, and every 30 days per entry the runs, the constructions posted and what the AI countries hold. |
| `country_upgrade` | Ctrl+Shift+click on a province building's Expand button expands it in every province of the player's country where the game allows it, while the treasury covers the running total (one province per state for `one_per_state` buildings). The Expand tooltip gets a line for it under vanilla's Ctrl+click line (localisation key `SE_PROVINCE_VIEW_BUILD_COUNTRY_TOOLTIP`). The clicked province's own button must be enabled, as for Ctrl+click. Multiplayer-safe: it posts the same commands as clicking each province. |
| `income_modifiers` | `rich_income_modifier`, `middle_income_modifier` and `poor_income_modifier` scale what the pops of that strata earn: artisan sales, the pay from factories and RGOs, pensions and unemployment benefit, after income tax. The country's modifiers (technologies, inventions, reforms, national value, country modifiers) count as well as the province's. Vanilla reads only the province's and applies them to the artisan's input money instead of income, so they barely do anything; that use is switched off. Changes results; integer arithmetic, the same on every machine. |
| `demand_province` | A new `cb_types.txt` keyword for mods: `po_demand_province = yes` in a CB works like `po_demand_state` (the same AI, infamy and texts), but the war-goal dialog lists the target's provinces instead of its states (`allowed_states` is checked per province, with the province in scope), and the peace treaty takes only the chosen province, and the war goal costs only that province's share of war score. The country is annexed only when that province is all it owns. Infamy is the `po_demand_state` define times the CB's `badboy_factor`, so set that factor for the smaller demand. Without the plugin the game logs the keyword as unknown and the CB has no peace option. Every player in a multiplayer game needs the plugin. |
| `event_background` | `background = "war"` in an event's body (next to `picture`) replaces the background of that event's window with `gfx/pictures/events/backgrounds/war.dds` (or `.tga`), found as the game finds a picture: in the mod first, then in the game. Nothing to declare. The sprite is stretched to the window's background, so size it for the window the event uses (country, province, `major` or `election`). Without the plugin the game logs the line as an unexpected key and shows the default background. Display only. |
| `change_flag` | A new effect: `change_flag = "USA/USA_51"` gives the scope's country the flag `gfx/flags/extra/USA/USA_51.tga` (or `.dds`), found as the game finds a picture: in the mod first, then in the game. The flag is not tied to a government: it stays when the government changes, until `change_flag = default` gives back the government's flag. Folder and file names use letters, digits and `_` only, with no `__`. Any size works: the image is stretched over the flag like any other. It is kept in the save as the country flag `se_flag_USA__USA_51` (`/` written as `__`), so `has_country_flag = se_flag_USA__USA_51` tests it. A missing file is logged once, and the country keeps its normal flag. The tooltip reads `SE_CHANGE_FLAG_EFFECT` (`$FLAG$` = the path) or `SE_CHANGE_FLAG_DEFAULT_EFFECT`. Display only: every machine runs the effect the same way. Without the plugin the game logs the effect as unknown. |
| `province_flags` | `has_province_flag = name` works in a province scope: true when `set_province_flag = name` was run on that province and not cleared since (false in a scope with no province). The engine has the keyword and the localisation (`HAVE_PROVINCE_FLAG`) but no trigger: it logged an unknown trigger and dropped it, so the condition was always true, even inside `NOT`. `set_province_flag` and `clr_province_flag` already worked and are saved with the province. Changes results of scripts that use it; reads game state only, the same on every machine. |
| `treaty` | Peacetime treaties. A "Propose Treaty" button in the diplomacy view (an 18th action, right of "Command Units"; "Justify war" is untouched) opens an editor where the player demands and offers terms (CBs with `is_treaty_cb = yes`: the plugin's `common/se_treaty_cbs.txt` adds 11, such as ceding a state or province, tributes, joining a sphere, becoming a puppet, releasing a puppet, freeing peoples; a mod may mark its own) and money. The other country answers in the vanilla box; the AI weighs what it gains against what it gives, relations and power (`treaty_ai_*` keys; `yesmen` makes it accept). `treaty_infamy = 1`: a term costs its CB's infamy. The script effects `dismantle = TAG`, `dismantled_by = TAG` and their `_gw` forms (Great War) make the winner impose a forced treaty on the loser after the peace, with a budget of points by the loser's rank (`treaty_dismantle_*` keys); a human dismantler picks the terms in a forced editor, an AI dismantler picks them itself; one forced treaty per loser per peace, however many goals carry the effect. `po_partition = yes` in a treaty CB lets the land become countries listed in the mod's `common/se_partitions.txt`. Every player in a multiplayer game needs the plugin. |
| `vassal_map_color` | On the political map a substate or puppet is drawn in a blend of its overlord's colour and its own: `vassal_map_color_own` percent of its own (default 30; 0 = the overlord's colour, 100 = vanilla). Each vassal stays distinct while reading as a shade of its overlord; occupation stripes use the same shades. Uses the current colour of both countries, per-government colours included. Display only: the country's colour and saves are untouched. |

## Tuning keys

The keys of `mod/script_extender/script_extender.ini` that set a number rather than turn a feature on. The AI treaty score
is `gain - give + relation + power - reluctance`, in war-score points (the peace cost of each term);
the AI accepts when it is at least 0. Details: `docs/re/systems/treaties.md` ("AI evaluation", "The
`dismantle` / `dismantle_gw` effects").

| ini key | default | meaning |
|---|---|---|
| `vassal_map_color_own` | 30 | percent of a vassal's own colour in its map colour (0 = the overlord's, 100 = vanilla) |
| `treaty_infamy` | 1 | a term costs its CB's infamy, as a war goal |
| `treaty_ai_reluctance` | 10 | points the AI wants on top of a fair deal |
| `treaty_ai_give_weight` | 150 | percent: what the AI gives counts this much against it (above 100: fair deals get refused) |
| `treaty_ai_money_per_point` | 100000 | pounds the AI counts as one war-score point |
| `treaty_ai_relation` | 8 | points at the best relation, minus that at the worst |
| `treaty_ai_core_gain` | 2000 | percent: worth of a province with its core when the AI gets one |
| `treaty_ai_core_give` | 4000 | percent: worth of a province with its core when the AI gives one |
| `treaty_ai_power` | 5 | points per 100% the proposer is stronger than the AI |
| `treaty_ai_power_cap` | 300 | percent of advantage counted at most |
| `treaty_ai_neighbour` | 200 | percent of the power term when the two border each other |
| `treaty_ai_coerce_ratio` | 300 | from this percent of the AI's strength, a bad relation helps the proposer, not the AI |
| `treaty_ai_hostile` | 100 | percent of the relation points it then gives |
| `treaty_ai_third_party` | 0 | percent of value of a gained term for a third country (liberate, release) the AI has no stake in (stake = borders the AI, in its sphere or its puppet, or holds the AI's cores) |
| `treaty_dismantle_leader` | 300 | points of a forced treaty when the dismantled country led a side of the war |
| `treaty_dismantle_gp` | 200 | points when it is a great power |
| `treaty_dismantle_secondary` | 150 | points when it is a secondary power |
| `treaty_dismantle_other` | 100 | points for any other country |
| `treaty_dismantle_gw` | 200 | percent: the points of `dismantle_gw` / `dismantled_by_gw` |
| `treaty_dismantle_infamy_min` | 10 | hundredths of infamy for the first point past the budget |
| `treaty_dismantle_infamy_max` | 100 | hundredths of infamy the cost per point rises towards |
| `treaty_dismantle_infamy_half` | 50 | points past the budget at which the cost is half of the way from min to max |
| `treaty_dismantle_ai_overspend` | 0 | points an AI dismantler goes past the budget at most |
| `treaty_dismantle_ai_max_states` | 35 | percent of the loser's non-colonial states an AI dismantler takes at most |
| `treaty_dismantle_ai_max_new` | 3 | countries an AI dismantler creates at most in one treaty |
| `treaty_dismantle_ai_border` | 150 | percent of a land term's value to an AI dismantler when it borders the gainer or land the treaty already gives it |
| `treaty_dismantle_ai_adjacent` | 120 | percent for the first state of a country yet to exist when it borders land the treaty gives to someone else (later states must border its own) |
| `treaty_dismantle_ai_exclave` | 0 | percent when it borders neither; 0 = never taken until a neighbour is (colonies and a new country's first state keep 100) |
| `treaty_dismantle_ai_ally` | 130 | percent of the value of a term whose third country is an ally of the AI (allied cores, colonies to an ally; colonies go to great or secondary powers only) |
| `treaty_dismantle_ai_coastal_only` | `europe` | continents (names of `map/continent.txt`) where an AI dismantler takes colonial states only on the coast or next to one it takes in the same treaty; empty = anywhere |
| `treaty_dismantle_ai_home_continent` | 130 | percent of the value of a colony on the continent of the AI dismantler's capital |
| `treaty_dismantle_ai_near_continents` | `asia` | continents where an AI dismantler prefers land near its own provinces |
| `treaty_dismantle_ai_near` | 50 | percent added to a land term's value there at distance 0, falling to `far` at `near_range` |
| `treaty_dismantle_ai_near_range` | 1500 | map pixels (`unit` positions of `map/positions.txt`) from which a land term there is worth `far` |
| `treaty_dismantle_ai_far` | 50 | percent of a land term's value there from `near_range` on |
| `treaty_dismantle_ai_colony_repeat` | 70 | percent: each colony the treaty already takes on a `near` or `coastal_only` continent scales the next one's value |
| `treaty_dismantle_ai_infamy` | 40 | percent of the value of a term that costs the AI dismantler infamy (land without a claim); 0 = never; it stays under `BADBOY_LIMIT` |
| `treaty_dismantle_ai_interest` | 200 | percent of the value of land listed for its gainer in the mod's `common/ai_interests.txt` (`TAG = { REGION ... provinces = { id ... } }`, as the partitions file): taken, or given to an ally, after cores and before colonies; a single province (cede province, for the AI or an ally) is taken only next to the gainer's land or land it gets in the treaty, interest land excepted |
| `treaty_dismantle_ai_frontier` | 150 | percent of the value of land freed to a third country (liberate, free peoples, partition) when it borders another country or the sea |
| `treaty_dismantle_ai_abroad` | 50 | percent of the value of the loser's homeland on a continent other than the gainer's: only next to land the gainer already owned, never chained through land it gets in the treaty (interest and colonies apart); 0 = never |
| `treaty_dismantle_log` | 0 | diagnostics: log every term an AI dismantler picks, with its value and the land rule that set it |

How it works, with the addresses: `docs/re/systems/map-modes.md` (map modes, political colours), `docs/re/systems/buildings.md` (province buildings, `country_upgrade`, `buildings_ai`), `docs/re/systems/casus-belli.md` (`po_demand_province`), `docs/re/systems/treaties.md` (`treaty`, the dismantle effects, partitions) and `docs/re/systems/effects.md` (how a plugin adds a script effect).

## Building

Built with the rest of the repository (`build.cmd`); turn it off with `-DBUILD_SCRIPT_EXTENDER=OFF`.
