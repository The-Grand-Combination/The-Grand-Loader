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

Party-rule offsets are `country+0xAC0 + 8·i`. **[C]** offsets, **[I]** which rule is which.

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

- **AI** (host only): `AI_ConstructionUpdate` (`0x008569A0`, fastcall, ECX = AI object). **[C]**
  - It caches the first province building of type 2 (`+0x78`), 4 (`+0x7C`) and 3 (`+0x80`).
  - It rolls the uncounted world MT once against chances at `g_economyDefines+0x60`.
  - It then calls `AI_BuildFort`, `AI_BuildNavalBase` or `AI_BuildRailroad`. The province pickers are
    `AI_PickFortProvince` (owned, not colonial, passes `Province_CanExpandBuilding`, money > 2× cost)
    and `AI_PickRailroadProvince` (the state with the most factories; also invests abroad).
  - Each builder posts the same constructor + `NetSession_QueueCommand`.
  - **The AI builds only the first type-4 building (the fort), never TGC's university or bank.** Pops
    can still build those through pop projects.
- **Effects** run in lockstep, instantly, with no command and no cost: **[C]**
  - `CBuildingEffect` (Execute `0x00891CA0`) calls `Province_SetBuildingLevel(value + pb+0x10)`. The
    `+0x10` is odd: **[?]**.
  - `CInfrastructureEffect` (`0x008948C0`) does the same on `db+0x34`.
  - `CBuildProvinceBuildingInCapitalEffect` (`0x008A49E0`) raises `db+0x34` / `db+0x38` by one in the
    capital or the capital's state.
- **Console:** the "instant construction" toggle (`0x012586D6`) applies only to the local player's
  constructions and desyncs multiplayer. **[C]**

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
8. The names of the AI chance fields at `g_economyDefines+0x60`.
