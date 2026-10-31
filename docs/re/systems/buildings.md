# Province buildings

Found in this repo (2026-10-23, Ghidra and decompilation; hook-site bytes checked against the exe).
Researched for the Script Extender's bulk building upgrade. Factory gates are in
[factories.md](factories.md); the command flow is in [multiplayer.md](multiplayer.md).

## Summary

- `buildings.txt` fills `g_buildingDb`. Province buildings (`province = yes`) get their own index
  `CBuilding+0x134`. Each province holds one `CProvinceBuilding` per index at `prov+0x118`, with the
  level ×1000 at `+0x20`. **[C]**
- `type =` is an enum: `factory` 0, `rgo` 1, `infrastructure` 2, `naval_base` 3, and **anything else
  4** (fort). TGC's `type = university` and `type = bank` are accepted as 4. **[C]**
- One check decides whether a building can be expanded: `Province_CanExpandBuilding`. It drives the
  expand button, vanilla's Ctrl-click, the AI, and the command's Validate. Validate runs on every
  machine just before Execute. **[C]**
- Ctrl-click on "expand" already builds in every eligible province of the same state. **[C]**
- Building is the command `CConstructBuildingCommand`, posted through the network session. A bulk
  upgrade that posts the same command per province behaves exactly like the clicks and stays in sync in
  multiplayer. **[C]**
- **TGC side effect:** `g_buildingDb+0x38` is meant to hold the fort but holds the last type-4 building
  loaded, which in TGC is the **bank**. `build_fort_in_capital`, `can_build_fort_in_capital` and the AI's
  fort scoring therefore read the bank. **[C]** code, **[I]** effect in game.

## Building database

`BuildingDb_Load` (`0x004DAED0`, called from the game-data loader at `0x0063C945`) handles each block of
`buildings.txt`. **[C]** For each block it:

1. creates a `CBuilding` with `CBuilding_ctor(name, fileIndex)`;
2. parses it (`vt[3]`, the keyword handler `CBuilding_OnKeyword`);
3. appends it to `db+0x0C`.

At the end of the file it resolves every entry (`CBuilding_Resolve`, `0x004D9AB0`, **[I]**).
`BuildingDb_Get` creates the database on first use.

| `g_buildingDb` (`0x0125CE80`, 0x3C bytes) | content | conf |
|---|---|---|
| `+0x00..+0x08` | name hash table | I |
| `+0x0C/+0x10/+0x14` | `vector<CBuilding*>`, all buildings, indexed by `+0x58` | C |
| `+0x1C/+0x20/+0x24` | `vector<CBuilding*>`, province buildings, indexed by `+0x134` (order of `build_icon` rows) | C |
| `+0x2C` | the null building (`g_nullBuilding`, `0x01258730`) | C |
| `+0x30` | the building named `naval_base` | C |
| `+0x34` | the last type-2 building (railroad) | C |
| `+0x38` | the last type-4 building (vanilla: fort; TGC: bank) | C |

### `CBuilding` (0x140 bytes, vtable `0x00DFDAC0`)

Values are ×1000 where fractional.

| offset | field | conf |
|---|---|---|
| `+0x08/+0x0C` | generic modifier value / definition: any unknown key that names a modifier; only the last one is kept | I |
| `+0x10` | `type` (token 0xD9) | C |
| `+0x14` | activating technology | I |
| `+0x18` | 9999 at construction | C |
| `+0x20` | name (`std::string`) | C |
| `+0x58` | file index; also the index of the per-country tech arrays | C |
| `+0x5C` | `cost` | C |
| `+0x60` | `goods_cost` (`CGoodsPool`) | C |
| `+0xB8` | `time` (days) | C |
| `+0xBC/+0xBD/+0xBE` | `capital` / `port` / `one_per_state` | C |
| `+0xC4` | `max_level` | C |
| `+0xC8` | `colonial_points` | C |
| `+0xD8/+0xD9` | `sail` / `steam` | C |
| `+0xDC` | `colonial_range` | C |
| `+0xE0/+0xE1` | `onmap` / `visibility` | C |
| `+0xE8`, `+0xF8` | prerequisites (resolved, names) | I, C |
| `+0x108`, `+0x118` | `not_if_x_exists` (resolved, names) | I, C |
| `+0x12C` | `production_type` | C |
| `+0x130` | `default_enabled` | C |
| `+0x131` | `province` | C |
| `+0x134` | province-building index (−1 when not a province building) | C |
| `+0x138/+0x139` | `pop_build_factory` / `spawn_railway_track` | C |
| `+0x13A/+0x13B` | `advanced_factory` / `strategic_factory` | C |
| `+0x13C` | `infrastructure` | C |

`fort_level`, `naval_capacity`, `movement_cost`, `local_ship_build`, `research_points_modifier` and
`local_rgo_output` have no field of their own. They go through the generic-modifier branch, so a
building probably keeps only one of them (TGC `naval_base` and `bank` have two). **[I]**, to confirm.

Per country, the tech state `country+0xBCC` holds two arrays indexed by `bld+0x58`:

- `+0x2FC`: byte "building activated" (`activate_building`);
- `+0x08`: **a pointer** to an `int[]` of max levels (`max_<building>`). `MapColor_Infrastructure` reads
  `*(*(country+0xBCC) + 8) + idx*4`. **[C]**

The terrain reduction (`min_build_<building>`) is the byte at `prov+0x5C -> +0x14 -> +0x34 [bld+0x58]`.
**[I]**

The highest level a country can reach in a province right now is therefore
`min(max_level, techMax − terrain reduction)`, the limit that `Building_CanBuildInProvince` applies
(condition 3 below). The Script Extender's building map mode colours a province solid blue at that level.
**[C]** formula, from the checks.

## In the province

| `CProvince` | content | conf |
|---|---|---|
| `+0xD8/+0xDC/+0xE0` | constructions (head, tail, count); nodes `{entry, ?, next}`, see below | C |
| `+0x110` | must be 0 for a foreign railroad investment | ? |
| `+0x118` | `vector<CProvinceBuilding*>`, indexed by `bld+0x134` | C |
| `+0x128` | owner tag (index at `+0x12C`) | C |
| `+0x158` | coastal (a 32-bit int, non-zero: the `port` check) | I |
| `+0x188` | `CState*`: `+0x48` province ids, `+0x60` state buildings, `+0x84` colonial, `+0x1C8` pop projects | C/I |
| `+0x2B8` | infrastructure value (shown by the infrastructure tooltip) | C |

| `CProvinceBuilding` (0x28 bytes, vtables `0x00DFDB20`/`0x00DFDB4C`) | content | conf |
|---|---|---|
| `+0x14` | modifier definition | C |
| `+0x18` | `CBuilding*` | C |
| `+0x20` | built level ×1000 | C |
| `+0x24` | effective level ×1000: set to `+0x20` on completion, moved toward it daily by `Province_DailyUpdate` (fort repair after a siege); used for cost and modifiers | C |

Helpers: `Province_HasBuilding` (ECX = province, EAX = building; level > 0), `Province_GetBuilding`,
`Province_SetBuildingLevel` (thiscall `(prov, levelX1000, recalc)`, EAX = building),
`Province_AddConstruction` (ESI = province, EDI = construction). **[C]**

### `CBuildingConstruction` (0x60 bytes, vtables `0x00DFC09C`/`0x00DFC0DC`)

| offset | content |
|---|---|
| `+0x38/+0x3C` | start / end date in hours (a day is 0x18) |
| `+0x40` | total days |
| `+0x44` | progress, percent ×1000 |
| `+0x48` | `CProvince*` |
| `+0x4C/+0x50` | builder tag / index |
| `+0x54` | `CGoodsProject*` while goods are pending, else null |
| `+0x58` | `CBuilding*` |

All **[C]**.

The province's construction list can hold other kinds of entries, so readers test each entry through its
vtable: slot 12 (`+0x30`, thiscall, returns bool) is "is a building construction" and returns 1 for
`CBuildingConstruction`; only then is `entry+0x58` a `CBuilding*`. `MapColor_Infrastructure` also calls
slot 13 (`+0x34`), which returns 0 for `CBuildingConstruction`; its meaning is **[?]**. **[C]** slots.

Every day, on every machine, `Province_DailyUpdate` advances each construction:

- **Before the end date:** `vt[14]` `BuildingConstruction_DailyUpdate` runs. While the goods project
  exists, the start and end dates move one day later.
- **At the end date:** `vt[9]` `BuildingConstruction_Complete` runs:
  - if the builder country is dead and not REB, the construction is discarded;
  - otherwise the level gains 1000 (capped at `max_level`), the effective level is set to it, the
    province is recalculated, and the owner gets `BUILDING_DONE`.

**[C]**

## Can it be expanded?

`Province_CanExpandBuilding` (`0x00511E70`): EDI = country, stack `(bld, prov)`, `RET 8`, result in AL.
**[C]** It checks:

1. The province's controller is its owner (not occupied). **It does not check that the country is the
   owner.**
2. `Building_ComputeExpandCost` <= the country's money.
3. `Building_CanBuildInProvince(country, prov, 0, 0, 0, 0, 0)`.

`Building_CanBuildInProvince` (`0x00511EF0`): EAX = building, stack
`(country, prov, popProject, ignoreTech, ignoreConstructing, useBuildingMaxOnly, ignoreRailRule)`,
`RET 0x1C`. **[C]** Conditions, with the tooltip key of each:

1. **Tech** (unless `ignoreTech`). Key: `BUILDING_NO_TECH`.
   - The building is activated by tech or is `default_enabled`.
   - Level + 1 <= the tech max (level 0 of a default-enabled building is exempt).
2. **Who builds.**
   - **Pop project:** the `pop_build_factory` party rule (`country+0xB00`) must be on.
   - **Own province** (country = controller): a `pop_build_factory` building needs the `build_railway`
     rule (`+0xAE8`) unless `ignoreRailRule`. Key: `BUILDING_NO_RAILWAYS`.
   - **Foreign province:** the controller is not REB. A `pop_build_factory` building also needs:
     - `Country_CanInvestIn`;
     - the `build_railway_invest` rule (`+0xB38`);
     - `prov+0x110 == 0`.

     Keys: `IS_NOT_GREAT_NATION`, `NOT_HOSTILE_BUILD`, `CAN_INVEST_IN_RAILROADS`. Forts and naval bases
     have no foreign restriction here.
3. **Max level:** level < `max_level`, and level < (tech max, or `max_level` with
   `useBuildingMaxOnly`) − terrain reduction. Keys: `BUILDING_AT_MAX`, `BUILDING_AT_MAX_TERRAIN`.
4. **Already building:** unless `ignoreConstructing`, no construction of the same building in the
   province. Different buildings can be built at once. Key: `BUILDING_ANOTHER`.
5. **Port:** a `port` building needs a coastal province. Key: `BUILDING_PORT_PROVINCE`.
6. **One per state:** a `one_per_state` building needs `Building_OnePerStateFree` (no other province of
   the state has it or is building it). Key: `BUILDING_ONE_PER_STATE`.
7. **Blocked by:** no `not_if_x_exists` building is present. Key: `BUILDING_BLOCKED_BY`.
8. **Must have:** every prerequisite is present. Key: `BUILDING_MUST_HAVE`.

Not checked anywhere: war, colonial status (only the AI skips colonial states) and the `capital`
flag. **[C]**

Party rule *i* is the byte at `country+0xAC8 + 8·i`, *i* in the order of the `CRuleDefinition` list
(`RULE_*` keys created at `0x00473235`..; token ids from the keyword table): **[C]**

| i | offset | rule | i | offset | rule |
|---|---|---|---|---|---|
| 0 | `+0xAC8` | build_factory | 11 | `+0xB20` | build_factory_invest |
| 1 | `+0xAD0` | expand_factory | 12 | `+0xB28` | expand_factory_invest |
| 2 | `+0xAD8` | open_factory | 13 | `+0xB30` | open_factory_invest |
| 3 | `+0xAE0` | destroy_factory | 14 | `+0xB38` | build_railway_invest |
| 4 | `+0xAE8` | build_railway | 15 | `+0xB40` | can_invest_in_pop_projects |
| 5 | `+0xAF0` | factory_priority | 16 | `+0xB48` | pop_build_factory_invest |
| 6 | `+0xAF8` | can_subsidise | 17 | `+0xB50` | pop_expand_factory_invest |
| 7 | `+0xB00` | pop_build_factory | 18 | `+0xB58` | pop_open_factory_invest |
| 8 | `+0xB08` | pop_expand_factory | 19 | `+0xB60` | allow_foreign_investment |
| 9 | `+0xB10` | pop_open_factory | 20.. | `+0xB68`.. | voting rules, forced_disarmament, slavery_allowed (31) |
| 10 | `+0xB18` | delete_factory_if_no_input | | | |

Every offset the code reads fits this table (`+0xAE8` in the railroad test above, the AI's flags below).

### Cost

- `Building_ComputeExpandCost`: EDX = province, ESI = out `i64*`, stack
  `(bld, country, country+0x1184)`, `RET 0xC`. It uses the effective level + 1. **[C]**
- It calls `Building_ComputeCost`. **[I]** formula:
  - goods: `goods_cost` × (1 − `country+0xE70`) at world prices;
  - plus, from level 2 up: `cost` × 2^(level−2).

### Triggers

- **`can_build_in_province`** (`CCanBuildInProvinceTrigger`, vtable `0x00E32D18`, Evaluate
  `0x00902790`):
  - calls `Building_CanBuildInProvince(country, prov, 0, unciv, 0, unciv, unciv)` with
    `unciv = !civilized`;
  - with `limit_to_world_greatest_level` (`+0x1C`), it also requires the level to be below the
    highest tech level held by any living, non-REB country;
  - it checks neither money nor occupation.
- **`can_build_*_in_capital`** (`CCanBuildProvinceBuildingInCapitalTrigger`, Evaluate `0x00902A40`):
  reads `db+0x34` / `db+0x38`.
- **`has_building`** (`CHasBuildingTrigger`, Evaluate `0x008CE600`): level > 0.

All **[C]**.

## The province window

- `CProvinceView_ctor` (`0x0073CF00`) builds `province_view`. The province pointer is at `view+0x24`.
  **[C]**
- `CProvinceView_Update` fills one row per province building:
  - a building under construction shows `building_progress` (`+0x44`/1000; 0 while goods are pending)
    and `underconstruction_icon`;
  - otherwise it shows `expand`, enabled exactly when `Province_CanExpandBuilding(localCountry, bld,
    prov)` is true (call at `0x0074356E`).

  **[C]**
- `CProvinceView_OnTooltip` builds the expand tooltip with `Building_BuildExpandTooltip`
  `(country, out, bld, prov)`. Unless the building is `one_per_state`, it then appends
  `PROVINCE_VIEW_BUILD_ALL_TOOLTIP` (`MOV EDX,0x00E13314` at `0x0074793B`). **[C]**

`CProvinceView_OnExpandClick` (`0x0073D6D0`, ECX = view, `[ESP+4]` = button, `RET 4`):

```c
bld = db->provinceBuildings[button->vt[0x80]()];
if (!input->vt[0x30]() /* Ctrl */ || bld->one_per_state)
    post(bld, view->province->id);               // no pre-check
else
    for (id in view->province->state->provinceIds)
        if (Province_CanExpandBuilding(localCountry, bld, provinces[id]))
            post(bld, id);
```

Here `input` is `*(idler+0x1C)`; its `vt[0x30]` is Ctrl and `vt[0x28]` is Shift. **[C]**
`CForeignProvince_OnExpandClick` (`0x0074A030`) is the same handler for the foreign-province window,
with the province at `this+0x134`. **[C]**

Limits of vanilla's Ctrl-click: **[C]**

- it covers one state, in province-id order;
- it does nothing extra for `one_per_state` buildings;
- it keeps no running money total, so each pre-check uses the treasury at click time and Validate later
  drops the commands the country can no longer afford;
- it only runs when the clicked province's own button is enabled.

## `CConstructBuildingCommand`

0x58 bytes, vtable `0x00E00C34`, type id 0x26E (`constructbuilding`). **[C]**

| offset | field | wire key |
|---|---|---|
| `+0x04..+0x3B` | `CCommand` base | |
| `+0x3C` | `CBuilding*` | `building` (sent as `bld+0x58`) |
| `+0x40` | province id | `province` |
| `+0x44/+0x48` | construction id: `word DAT_01258A90 + 0x1268` / `++DAT_01258A9C` | `id` |
| `+0x4C/+0x50` | country tag / index | `country` |
| `+0x54` | pop project | `pop_build_factory` |

| slot | function |
|---|---|
| ctor | `ConstructBuildingCommand_ctor` (`0x0057B560`): stdcall `(this, bld, provId, tag, countryIdx)`, `RET 0x14`, returns EAX = this; main thread only (global id counter) |
| ctor (pop) | `0x0057B640`, from the pop investment code (host only) |
| 2 | Serialize `0x0057BC50` |
| 4 | Deserialize `0x0057BDF0` |
| 6 | Execute `0x0057B7E0` (thiscall) |
| 7 | type id `0x0057BC40` |
| 12 | keep for joiners: 0 (`0x0058CFD0`) |
| 13 | Clone `0x0057BEE0` |
| 14 | Validate `0x0057BF50` (thiscall): pop project → `Building_CanBuildInProvince(country, prov, 1, 0...)`, else `Province_CanExpandBuilding` |

All **[C]**.

Lifecycle: **[C]**

1. The UI or the AI allocates the command (`operator_new(0x58)`), constructs it and passes it to
   `session->vt[0x18]` (`NetSession_QueueCommand`), which takes ownership. The session is
   `*(idler->vt[0x44]() + 0x38)`.
2. The host stamps the tick and broadcasts it.
3. On every machine, `CommandQueue_Process` runs Validate, then Execute only if Validate is true.
4. **Execute** (`ConstructBuildingCommand_Execute`):
   1. **Payment.**
      - Normal build: `Country_PayForBuilding` (stdcall `(country, bld, prov)`, `RET 0xC`) calls
        `Country_SpendMoney` (EAX = country, EDI = out, stack `(i64, category 4)`), which borrows when
        short. The amount is booked in the owner's foreign-investment ledger (`+0x1580`), or a message
        is posted for a foreign investment.
      - Pop project: the state's pop projects pay instead.
   2. **Construction object.** A `CBuildingConstruction` is created with the command's id, plus a goods
      project with the cost modifier.
   3. **Dates.** Start = today, end = today + `time`.
   4. **Registration.** `Province_AddConstruction`, then `Country_Recalculate`, then the map is marked
      dirty.
   5. **UI refresh** for the local builder.

`CConstructStateBuildingCommand` (vtable `0x00E00C74`, type 0x84E, ctor `0x0057C130`, Execute
`0x0057C400`, Validate `0x0057CAF0`) is the factory version. Its Ctrl/Shift bulk handler (`0x006E45F0`)
keeps a running money total. **[C]**

## Other builders

- **AI** (host only): `AI_ConstructionUpdate`, see [The AI builder](#the-ai-builder). It builds three
  province buildings only, chosen by type, not by name.
- **Effects** run in lockstep, instantly, with no command and no cost: **[C]**
  - `CBuildingEffect` (Execute `0x00891CA0`) calls `Province_SetBuildingLevel(value + pb+0x10)`. The
    `+0x10` is odd: **[?]**.
  - `CInfrastructureEffect` (`0x008948C0`) does the same on `db+0x34`.
  - `CBuildProvinceBuildingInCapitalEffect` (`0x008A49E0`) raises `db+0x34` / `db+0x38` by one in the
    capital or the capital's state.
- **Console:** the "instant construction" toggle (`0x012586D6`) applies only to the local player's
  constructions and desyncs multiplayer. **[C]**

## The AI builder

Found 2026-10-31 (decompilation of `0x008569A0..0x00859550`). Researched before teaching the AI to build
other province buildings.

### Summary

- One job per AI country, `AI_ConstructionUpdate`, decides every AI construction: forts, naval bases,
  railroads, factories (build / expand / reopen / delete), foreign investment, and pop-project money.
  **[C]**
- **The province buildings are found by `type`, not by name.** On its first run the job takes, in
  province-building order (`db+0x1C`, the order of `buildings.txt`), the first building of type 2, 4 and
  3. It keeps them for the life of the AI object. **[C]**
- The fort builder knows nothing about forts. It takes the cached type-4 building and scores provinces by
  capital, borders and hostile neighbours. **Any type-4 building listed before the fort is built as if it
  were the fort, in border provinces. Type-4 buildings after it are never built by the AI.** **[C]**
- One roll per run against the `CHANCE_*` defines decides which builders run. Nothing is built at war.
  **[C]**

### The job

`AI_ConstructionUpdate` (`0x008569A0`, fastcall, ECX = the job): **[C]** unless noted.

| job field | content |
|---|---|
| vtable | `0x00E284EC` (RTTI just before it); slot 6 (`+0x18`, at `0x00E28504`) = `AI_ConstructionUpdate`, slot 3 `0x00825CF0` calls slot 6, slot 5 `0x00826480` is the countdown tick |
| `+0x30` | countdown: set to `Rng_Next() % 17 + 10` (10–26) on every run; **[I]** the days to the next run |
| `+0x34` / `+0x38` | country tag / index |
| `+0x68` | day of month of the last run: the body runs at most once a day |
| `+0x6C..+0x74` | party rules of the country, copied on each run (below) |
| `+0x78` / `+0x7C` / `+0x80` | cached first type-2 (railroad), type-4 (fort), type-3 (naval base) building |

Rule flags (rule offsets in [Can it be expanded?](#can-it-be-expanded)):

| flag | from | used by |
|---|---|---|
| `+0x6C` | build_factory | factory builder |
| `+0x6D` | expand_factory | factory builder |
| `+0x6E` | open_factory | factory builder (reopen) |
| `+0x6F` | build_railway | railroad builder |
| `+0x70` | destroy_factory | factory deletion |
| `+0x71` | great power **and** build_factory_invest | foreign investment |
| `+0x72` | great power **and** expand_factory_invest | foreign investment |
| `+0x73` | can_invest_in_pop_projects | pop-project money |
| `+0x74` | build_railway_invest | foreign investment, railroads abroad |

The order of one run:

```c
if (country->atWar /*+0x67C*/) return;            // all builders, deletion included
ai = g_economyDefines->ai;                        // +0x60
naval = ai->CHANCE_BUILD_NAVAL_BASE;              // +0x38
if (country->colonialStates /*+0x7D8*/ < 1 && !country->greatPower /*+0x31*/)
    naval /= 3;
r = worldMT() % 1000;                             // one draw for every builder below
if (r <= ai->CHANCE_BUILD_FORT /*+0x3C*/)       AI_BuildFort();
if (r <= naval)                                 AI_BuildNavalBase();
if (build_factory || expand_factory || open_factory) AI_BuildFactory(job);   // no chance
if (build_railway && r <= ai->CHANCE_BUILD_RAILROAD /*+0x34*/)  AI_BuildRailroad(own country);
if ((factory_invest flags || build_railway_invest) && r <= ai->CHANCE_FOREIGN_INVEST /*+0x44*/)
    AI_ForeignInvest(job);
if (can_invest_in_pop_projects && r <= ai->CHANCE_INVEST_POP_PROJ /*+0x40*/)
    AI_InvestPopProject(job);
if (destroy_factory) delete the worst factory of a state with more than 6;
```

The defines are `defines.lua`'s `ai` block in file order, 4 bytes each (`+0x24`
`ONE_SIDE_MAX_WARSCORE`, `+0x28` `POP_PROJECT_INVESTMENT_MAX_BUDGET_FACTOR`, `+0x34..+0x44` the five
`CHANCE_*`), ×1000. A chance of 1.0 always passes. The roll reads the world MT directly (`world+0xB4`,
index `+0xA74`) without the call counter, on the host only. **[C]**

`country+0x7D8` counts the states with `CState+0x84 > 0` (colonial), recounted by
`Country_Recalculate` (`0x00515490`). **[C]**

### The province-building builders

Each builder picks a province, checks the money and posts `ConstructBuildingCommand_ctor` +
`NetSession_QueueCommand`, as a player would. Validate then runs on every machine
(`Province_CanExpandBuilding`). **[C]**

| builder | picker | money test |
|---|---|---|
| `AI_BuildFort` `0x00858560` | `AI_PickFortProvince` `0x00857E60` | money > 2 × `Building_ComputeExpandCost` |
| `AI_BuildNavalBase` `0x00858460` | `AI_PickNavalBaseProvince` `0x008577B0` | money > cost |
| `AI_BuildRailroad` `0x008582D0` (ECX = the country that owns the land) | `AI_PickRailroadProvince` `0x008580E0` | own land: money > cost; abroad: money > 3 × the foreign-investment cost |

**Fort** (`AI_PickFortProvince`, arg = job): for each owned province **[C]**:

1. skip a colonial state (`state+0x84 > 0`) and a province where `Province_CanExpandBuilding(+0x7C)` fails;
2. score: +500 for the capital (`country+0xBE4`); +500 when the province is in the list of `0x0050E450`
   (EAX = country): among the groups at `country+0xA08`, the one holding the capital (`prov+0xE8`), else
   the one with the highest `+0x30`. **[C]** code, **[I]** the group is the home land mass;
3. for each land neighbour (`prov+0x5C → +0xC8`, 0x24-byte entries, `[0]` kind, `[1]` province id) owned by
   another country: +200 when the kind is 1; then `a + b/4` from two per-country tables of the AI state
   (`country+0x3B8/+0x3BC` and `+0x3F0/+0x3F4`, keyed by the neighbour's index). The first is also read
   next to `AI_GetBaseRelation` by the AI's answer to diplomatic actions (`0x00844050`, `0x00844140`): an
   attitude value, sign **[?]**; when that is ≥ 150, it is added (×2 for kind 1, ×1.5 for kind 2);
4. the highest score above 0 wins.

**Naval base** (`AI_PickNavalBaseProvince`): **[C]** structure, **[I]** meaning of the helpers.

1. First, an upgrade: in each state, the coastal province (`prov+0x158`) that already has the building and
   scores best by `0x0051F2E0`, the base's supply score:
   `NAVAL_BASE_SUPPLY_SCORE_BASE` × 2^(level − 1) (`NAVAL_BASE_SUPPLY_SCORE_EMPTY` at level 0), ×
   `NAVAL_BASE_NON_CORE_SUPPLY_SCORE` when the province is not a core of the owner and lies outside the home
   group (country defines `+0xE0`, `+0xE4`, `+0xE8`, **[C]**); it must be a province the country
   controls, affordable, and pass `Building_CanBuildInProvince`.
2. Otherwise a new base: the existing levels are summed into 8 directions × 3 distances from the capital
   (distance in units of the building's `colonial_range`). Every affordable coastal province that passes
   `Building_CanBuildInProvince` scores `5 × levels in its sector + worldMT() % 3`; the lowest wins.
   **This draws the world MT once per candidate, on the host.**

**Railroad** (`AI_PickRailroadProvince`, args job + the country whose states are searched): in each state,
the first province the owner controls whose cost (plus 25 × the input value of a `production_type`, if
any) is within the money and that passes `Building_CanBuildInProvince`; the state with the highest
`state+0x68` wins (**[I]** its factory count). **[C]**

None of the three looks at the building's own data beyond `Building_CanBuildInProvince` and the cost, so
they would build any building given to them. **[C]**

### The other builders

**[C]** structure; names of the helpers **[I]**.

- `AI_BuildFactory` (`0x00858670`): `AI_PickFactoryType` (`0x00857430`, every building whose `vt[0x24]`
  is true, best `AI_FactoryScore` `0x008571A0`: output price against input prices, plus the country's
  factory modifiers), then `AI_PickFactoryState` (`0x00857530`). It then does one of three things: it
  reopens a closed factory of that type (open_factory, command at `0x005827F0`); it expands one
  (expand_factory, `CConstructStateBuildingCommand` `0x0057C130`); or it builds a new one (build_factory,
  civilized, `0x0057C080`).
- `AI_ForeignInvest` (`0x008589B0`): target from `AI_PickInvestTarget` (`0x00858D90`). The target is a
  civilized, non-great power that allows foreign investment (`+0xB60`), is not at war with the investor
  and scores best (score ≥ 11). The investor then builds or expands a factory there, then
  `AI_BuildRailroad(target)` with build_railway_invest.
- `AI_InvestPopProject` (`0x00859180`): money for the state pop project that needs it most, at most
  `POP_PROJECT_INVESTMENT_MAX_BUDGET_FACTOR` × money (command at `0x00588650`).
- Deletion: in states with more than 6 factories, the closed or unprofitable factory with the lowest
  `AI_FactoryScore` (command `0x0057CC60`).

### Why a building's position in buildings.txt matters

- **Before the fort:** it is the job's `+0x7C`. The fort builder builds it in border provinces, with the
  fort's chance and its 2 × cost margin, and never builds the fort. **[C]**
- **After the fort:** the AI never builds it. Only pop projects and script effects can. **[C]**
- **Last of type 4:** it is `db+0x38`, which `build_fort_in_capital`, `can_build_fort_in_capital` and the
  fort-in-capital effect use instead of the fort. **[C]**
- The same holds for a second type-2 building (the railroad builder takes the first) and a second type-3
  building (the naval base builder takes the first). **[C]**

### Adding builders natively: what the code allows

- **Hook point:** the vtable slot `0x00E28504` (slot 6 of `0x00E284EC`). A wrapper calls the original,
  then runs extra builders with the same job (`+0x34/+0x38` country, `+0x6C..` rules). The job is always
  an AI country on the host. **[C]** slot.
- **Posting:** the same `ConstructBuildingCommand_ctor` + session `vt[0x18]` as the existing builders and
  the bulk upgrade above. Validate on every machine keeps multiplayer in sync. Only the host needs the
  code. **[C]**
- **Randomness:** the job lives on the AI object, which exists only on the host
  ([multiplayer.md](multiplayer.md)). So its draws never have to match a client's: only the commands
  travel, and every machine validates them. **[I]** host-only, from where the AI objects are created.
  - **Never the global MT** (`g_mtState`, counted in `g_rngCalls`). A host-only draw there moves the host's
    count and the daily check reports OOS.
  - **The world MT** is per machine anyway: clock seed, never saved. The roll and the naval-base picker
    read it inline (tempering code, not `Rng_Next`), so `sync_world_rng` does not see them. Only the
    countdown's `Rng_Next` (`0x008569CF`) is one of its 20 sites. A new builder gains nothing from it.
  - A private RNG is right for host-only code. It would **not** be right for code that runs on every
    machine (`AI_DailyUpdate`, the tick): there a draw must come from the counted global MT, and the
    result must change state directly, not through a command that every machine would post.
- **Which building:** the cache can be steered at `+0x7C`: write the fort after the first run, or
  patch the type-4 search (`CMP [EDI+0x10],4` at `0x00856CB3`; type 2 at `0x00856C02`, type 3 at
  `0x00856DC2`) to compare the name. Then every other type-4 building needs a builder of its own, with
  its own scoring and chance. The fort scoring (borders, threat) fits no economic building. **[C]**
  search, design **[I]**.
- **War:** vanilla builds nothing while `country+0x67C` is set. A new builder can keep that or not.

### Built: `buildings_ai` (Script Extender, 2026-10-31)

`script_extender/src/features/buildings_ai.cpp`, ini key `buildings_ai`. Built, not yet tested in game.

- **Hook:** `0x00E28504` points to a wrapper. Before the original, it fills the job's `+0x78/+0x7C/+0x80`
  (when still empty) with the buildings named `railroad` / `fort` / `naval_base`, if each has the
  expected type. The vanilla builders therefore keep their building wherever it sits in `buildings.txt`.
  After the original, when `+0x68` changed (the body ran), the file's entries run.
- **The file:** `common/buildings_ai.txt`, from the first `-mod=` folder that has it, else the game's. It
  is read with the game's parser on the first run (`ParserBlock` + `Persistent_ReadBlock` on objects of
  our own whose slot 4 gets each `key = value`; slot 5 is the empty `0x004010C0`). One block per
  province building, by name:

  ```
  bank = {
      chance = 0.15        # per run, 0..1
      money = 2            # treasury > money × Building_ComputeExpandCost (default 2)
      weight = {           # a CMeanTimeToHappen, as a decision's ai_will_do
          factor = 1
          modifier = { factor = 5 is_capital = yes }
      }
  }
  ```

  `weight` is a real `CMeanTimeToHappen` (`0x00E2CEB0`, 0x18 bytes, `+4 = 0x18D`, `+8 = 1000`), read by
  its own slot 4. So every trigger of the game works in its modifiers, with the game's own syntax checks
  and error lines.
- **A run, per entry:** country not human and not at war; then roll `chance` with splitmix64 seeded by
  (date, country, entry). The candidates are the owned provinces (owner = the country) where
  `Province_CanExpandBuilding` holds and the treasury minus what this run already posted covers
  `money` × the cost. Each is weighed in a province scope built as `EventManager_DailyScan` builds one
  (`+0x08 = "---"`, `+0x0C = 0`, `+0x10` = province id). The weight follows `CDecision_EvaluateAiWillDo`'s
  rule: base × the factor of every modifier whose triggers all hold, 0 below 1. One province is drawn in
  proportion to its weight and `ConstructBuildingCommand` is posted (`game/buildings.h`).
- Listing `fort`, `naval_base` or `railroad` adds a builder; the vanilla one keeps running.

## Bulk upgrade: how to do it

For each target province, on the main thread, as the local player:

```c
CCountry* c = countries[world+0xB60];
if (p->ownerIndex /*+0x12C*/ != c->index /*+0x20*/) skip;   // the game does not check this
if (!Province_CanExpandBuilding(EDI = c, bld, p)) skip;
cmd = operator_new(0x58);
ConstructBuildingCommand_ctor(cmd, bld, p->id /*+0x58*/,
                              *(u32*)(world+0xB5C), *(int*)(world+0xB60));
session = *(idler->vt[0x44]() + 0x38);
session->vt[0x18](cmd);                                     // NetSession_QueueCommand
```

- **Scopes:**
  - a state: `p->state(+0x188)->+0x48`;
  - the whole country: `c+0x9D8`;
  - a `one_per_state` building: at most one province per state.
- **Money:** keep a running total with `Building_ComputeExpandCost`, so commands the country cannot
  afford are not posted.
- **Multiplayer:** only the command changes state, and every machine validates it again. The wire
  format is unchanged, so only the player who clicks needs the plugin.
- **Never** call Execute, the level setters, the world RNG or instant construction directly.

### Hook sites

| purpose | site | original bytes | notes |
|---|---|---|---|
| expand click, own province | `0x0073D6D0` | `55 8B EC 83 E4 F8` | 6-byte detour; the next instruction (`6A FF 68 61 04 B5 00`) has a relocated imm32 |
| expand click, foreign province | `0x0074A030` | `55 8B EC 83 E4 F8` | same detour |
| Ctrl test inside the own-province handler | `0x0073D75E` | `8B 4B 1C 8B 11 8B 42 30 89 74 24 0C FF D0 84 C0` | no relocations; then `JZ 0x0073D85E`; EBX = idler, ESI = building |
| "build all" tooltip key | `0x0074793B` (also `0x0074EB63`, `0x00751FA8`) | `BA 14 33 E1 00` | rewording the localisation key is simpler |

Entry bytes of the functions the plugin calls (to verify before calling):

| function | entry bytes |
|---|---|
| `Province_CanExpandBuilding` `0x00511E70` | `55 8B EC 83 EC 10 53 8B 5D 0C 8B 83 34 01 00 00` |
| `Building_CanBuildInProvince` `0x00511EF0` | `55 8B EC 53 8B 5D 08 56 57 8B F8 80 BF 31 01 00` |
| `ConstructBuildingCommand_ctor` `0x0057B560` | `55 8B EC 64 A1 00 00 00 00 6A FF 68 9B 61 B4 00` |
| `ConstructBuildingCommand_Validate` `0x0057BF50` | `55 8B EC 83 E4 F8 8B 41 3C 83 EC 08 56 57 85 C0` |
| `Building_ComputeExpandCost` `0x004DA790` | `55 8B EC 83 EC 0C 53 8B 5D 08 33 C0 57 38 83 31` |

## Open questions

1. What clears `CBuildingConstruction+0x54` once the goods are bought (the market path is not traced).
2. Whether the host checks that a command's country belongs to the player who sent it.
3. The meaning of `prov+0x110` and `prov+0x158`.
4. Whether `CBuilding+0x128/+0x129` (bools for tokens 0x284/0xBF) are real fields.
5. Whether `CBuildingEffect` reading `pb+0x10` is a vanilla bug.
6. Whether a building keeps only one generic modifier. Test with TGC `naval_base`
   (`naval_capacity` + `local_ship_build`) and `bank`.
7. TGC `db+0x38` = bank: confirm in game. Reordering `buildings.txt` would make the AI build
   universities, so a loader patch that only takes the name `fort` is the better fix.
8. How the AI job is scheduled: who counts down `+0x30` and calls slot 3 (the base tick's own gates,
   slots 8 and 10, are empty in this class).
9. The fort scoring's inputs: what the groups at `country+0xA08` are, the neighbour kinds 1 / 2, and
   the sign and meaning of the AI tables at `country+0x3B8` / `+0x3F0`.
