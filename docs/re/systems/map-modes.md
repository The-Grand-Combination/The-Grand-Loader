# Map modes

Found in this repo (2026-10-23, Ghidra and decompilation; hook-site bytes checked against the exe).
Researched for the Script Extender's building map mode (infrastructure mode as a building selector).

## Summary

- There are 22 map modes (`MAPMODE` 0..21). Each has a menubar button `mapmode_N`, a handler that
  sets the mode, and a colour function that writes two ARGB colours per province. **[C]**
- Infrastructure is mode **4**, button `mapmode_6`. It only shows the railroad. **[C]**
- Colours go to the GPU as two 256×256 lookup textures (primary, secondary) with one pixel per
  province id. A secondary colour different from the primary is drawn as stripes. **[C]** layout,
  **[I]** shader.
- Infrastructure is never recoloured by the daily tick. Only a mode change or a building command
  (current mode 4 or 21) recolours it. **[C]**
- Every call into this code must run on the main thread, because the upload calls Direct3D.

## Objects

| object | how to reach it | conf |
|---|---|---|
| idler (`CInGameIdler`, vtable `0x00E0A34C`) | `*(g_world + 0xB24)` | C |
| map view | `g_mapView` (`0x012588DC`), or idler `vt[0x38]` = `CInGameIdler_GetMapView` | C |
| `CProvinceManager` | map view `+0x20BC` | C |
| province colour records | `vector<record*>` at map view `+0x20D8`/`+0x20DC`, indexed by province id (0 unused) | C |
| colour record | `+0` primary ARGB, `+4` secondary ARGB, `+0xC` `CProvince*` | C |
| map renderer | `[mapView + 0x3648]` | C |

The current mode is stored at idler `+0xDB4` and mirrored at `g_provinceSource + 0x2AD8`. The getter is
idler `vt[0x13C]` (`CInGameIdler_GetMapMode`). **[C]**

## Modes

| mode | button | name | handler | colour function |
|---|---|---|---|---|
| 0 | `mapmode_1` | terrain | `0x0066C290` | `0x007FC980` |
| 1 | `mapmode_2` | political | `0x0066C400` | `0x007FCA30` |
| 2 | `mapmode_4` | diplomatic | `0x0066C6F0` | `0x007FCCD0` |
| 3 | `mapmode_5` | region | `0x0066C870` | `0x008010D0` |
| **4** | **`mapmode_6`** | **infrastructure** | **`0x0066C9F0`** | **`0x008014D0`** |
| 5 | `mapmode_3` | revolt risk | `0x0066C570` | `0x00801230` |
| 6 | `mapmode_8` | administrative | `0x0066CCF0` | `0x008019B0` |
| 7 | `mapmode_7` | colonial | `0x0066CB70` | `0x00801C50` |
| 8 | `mapmode_9` | recruitment | `0x0066CE70` | `0x00801FF0` |
| 9 | `mapmode_10` | national focus | `0x0066E240` | `0x00802490` |
| 10 | `mapmode_11` | RGO output | `0x0066E120` | `0x00800D60` |
| 11 | `mapmode_12` | population density | `0x0066E000` | `0x00800880` |
| 12 | `mapmode_13` | nationality | `0x0066D4F0` | `0x00800690` |
| 13 | `mapmode_14` | sphere of influence | `0x0066D2B0` | `0x007FF880` |
| 14 | `mapmode_15` | supply limit | `0x0066DEE0` | `0x007FF310` |
| 15 | `mapmode_16` | party loyalty | `0x0066DDC0` | `0x007FEB80` |
| 16 | `mapmode_17` | ranking | `0x0066D610` | `0x007FE6B0` |
| 17 | `mapmode_18` | migration | `0x0066DA90` | `0x007FD390` |
| 18 | `mapmode_19` | civilization level | `0x0066D730` | `0x007FD860` |
| 19 | `mapmode_20` | relations | `0x0066D3D0` | `0x00800280` |
| 20 | `mapmode_21` | crisis | `0x0066D970` | `0x007FDAF0` |
| 21 | `mapmode_22` | naval base | `0x0066D850` | `0x007FE1F0` |

Only the infrastructure pair is named in `functions.csv` (`MapModeButton_Infrastructure`,
`MapColor_Infrastructure`); the other addresses are from the jump table and the handlers. **[C]**

Each handler also sets `CSettings` fields: `+0xF0` map style (7 for infrastructure and RGO output),
`+0xEC` layer mask (`0xFFBA` for most modes), `+0xE6` (1 only for national focus). **[C]** values,
**[?]** meaning.

## Buttons

- `CInGameIdler_ctor` builds one 0x50-byte `CButtonObserverGlue` per button from idler `+0xDB8`. The
  `mapmode_6` glue is at idler `+0xF48`. Its handler is loaded by `MOV EAX,0x0066C9F0` at `0x0064B151`
  (`B8 F0 C9 66 00`; the imm32 is relocated). **[C]**
- `Game_FinalizeLoadAndEnterGameplay` finds each child of the menubar (idler `+0x1630`, `vt[0x34]`) by
  name and registers the glue with `child+0x54 -> vt[4](&glue)`. **[C]**
- `MenuBar_SetMapModeButtons` (stdcall `(idler, N)`, `RET 8`) shows frame 2 on `mapmode_N` and frame 1 on
  the others. **[C]**
- Keyboard shortcuts are the `shortcut =` entries of `menubar.gui` and fire the same glue. **[I]**

### The glue (`CButtonObserverGlue<CInGameIdler>`)

- `ButtonObserverGlue_ctor` (EAX = glue, ECX = object) stores the object at `+4` and nine pairs
  `{handler, this-adjust}` from the stack at `+0x08..+0x4C`, then the vtable
  `CButtonObserverGlue_CInGameIdler_vftable` (`0x00E0A58C`, 10 slots). **[C]**
- Slot 0 is the destructor. Slots 1..9 each forward to one pair:
  `MOV EAX,ECX; MOV EDX,[EAX+k]; TEST EDX,EDX; JZ; MOV ECX,[EAX+k+4]; ADD ECX,[EAX+4]; JMP EDX`. **[C]**

  | slot | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
  |---|---|---|---|---|---|---|---|---|---|
  | pair at | `+0x38` | `+0x08` | `+0x20` | `+0x10` | `+0x18` | `+0x28` | `+0x48` | `+0x30` | `+0x40` |

  Slots 1, 7 and 9 keep a stack frame (`PUSH EBP ... POP EBP; JMP EDX`), so they probably pass stack
  arguments through. **[C]** code, **[I]** arguments.
- The map-mode glues fill only the first pair (`+0x08`, used by slot 2), with adjust 0. So slot 2 is the
  click. **[I]** The handler is called as a plain thiscall with no stack arguments: the `MapModeButton_*`
  handlers end with a plain `RET`. **[C]**
- The other eight pairs are room for other events (right click, hover, ...). Which slot is which event is
  **[?]**.
- `MapModeButton_Infrastructure` skips the click sound when the mode is already 4
  (`CMP [ESI+0xDB4],4; JZ`). The sound is `Sound_Get()->vt[0x60](str_click)`. **[C]**

## Setting the mode

`CInGameIdler_SetMapMode` (`0x00679A70`) takes **EAX = mode, ECX = idler**. It dispatches through the
jump table at `0x00679B1C`; values above 0x15 fall back to terrain. **[C]**

- Its call to the infrastructure handler returns to `0x00679AA2`. A handler that sees that return
  address was called programmatically, not by a click. **[C]**
- Do not detour its entry: a `JA rel32` starts at `+4` (`51 83 F8 15 0F 87 ...`). **[C]**
- `CInGameIdler_RefreshMapMode` (`0x00678A80`, idler `vt[0xDC]`) is
  `MOV EAX,[ECX+0xDB4]; JMP CInGameIdler_SetMapMode`. It redraws the current mode at once, without the
  click sound. Building commands call it when the mode is 4 or 21. **[C]**

`CStack<MAPMODE>` (vtable `0x00E0A5EC`, at idler `+0xBC8`, head `+0xBCC`, count `+0xBD4`) is a mode
history. `CInGameIdler_PushMapMode` (`vt[0x174]`) and `CInGameIdler_PopMapMode` (`vt[0x178]`, pops and
sets) have no direct callers. **[C]**

## Colouring

Every colour function is `void __stdcall (CProvinceManager*)`, `RET 4`. It loops over province ids
`>= 1` and writes `record+0` and `record+4`. **[C]**

Colours are built as a stack `CColor` (vtable `0x00DFBA7C`, floats r/g/b/a at `+8/+0xC/+0x10/+0x14`).
`CColor_ToARGB` (ECX = `CColor*`) returns `A<<24 | R<<16 | G<<8 | B`. **[C]**

### Infrastructure (`MapColor_Infrastructure`, `0x008014D0`)

The building is the first province building with type 2 (railroad), found in `g_buildingDb + 0x1C`
([buildings.md](buildings.md)). With `k` = 1.0 (`kOne_f32`, `0x00F092FC`, also the alpha), or 0.3
(`kMapDim_f32`, `0x00DEFC8C`) when the bytes `g_mapDimFlagA` and `g_mapDimFlagB` (`0x00F092FA/FB`, both 1 in
the file) are set and `[prov+0x5C]+0x360 < 1` (a province the player has not seen, **[I]**): **[C]**

| case | colour (r, g, b) |
|---|---|
| default, or `Building_CanBuildInProvince` false | khaki (0.7k, 0.7k, 0.4k) |
| level >= tech max − terrain reduction | red (0.8k, 0.2k, 0.2k) |
| otherwise | gradient (0.2·level·k, 0.1k, (1 − 0.2·level)·k) |
| a railroad construction in the province (`prov+0xD8`) | secondary yellow (stripes) |
| another check on that list fails | secondary red; meaning **[?]** |

The construction list is walked through each entry's vtable: slot 12 (`+0x30`) "is a building
construction", then `entry+0x58` = `CBuilding*`; the second check is slot 13 (`+0x34`). For a
`CBuildingConstruction` slot 12 returns 1 and slot 13 returns 0, so the red stripe probably marks a
province with a construction of another building. **[C]** slots, **[I]** meaning.

The tech max is `(country+0xBCC)->+8 [bld+0x58]` of the player country. The terrain reduction is the byte
`(prov+0x5C)->+0x14->+0x34 [bld+0x58]`. The level is `CProvinceBuilding+0x20 / 1000`. **[C]**

Constants: 0.7 `0x00E45978`, 0.4 `0x00E45918`, 0.8 `0x00E45770`, 0.2 `0x00E45598`, 0.1 `0x00E456D8`,
0.999 `0x00E45650`. **[C]**

The handler `MapModeButton_Infrastructure` is the colour function's only caller (`CALL` at `0x0066CABE`,
`E8 0D 4A 19 00`). **[C]**

### Political colours (2026-10-24)

Researched for the Script Extender's `vassal_map_color` (substates and puppets in a shade of their overlord).

The province record's owner is `CProvince+0x128/+0x12C` and its controller `+0x130/+0x134`, each a country
reference {tag, index}. Index 0 means no country. **[C]**

`MapColor_Political` (`0x007FCA30`, mode 1) does the following for each province:

- With no owner: white (1, 1, 1, 1) in both colours.
- Otherwise:
  - primary = `Country_GetMapColor(owner)` scaled by the dim factor `k`, the same as for infrastructure;
  - secondary = the controller's colour when the controller differs from the owner (occupation
    stripes), else the primary.

**[C]**

`MapColor_PoliticalPlain` (`0x007FCBD0`) is the same without `k`, and paints a province with no owner
grey (`0x99999999`). **[C]** Seven sites call it, with `CProvinceManager*` as the only stack argument:

- `CSetHistoryDate_Execute`;
- the thunk `MapView_ColorPolitical` (`0x00645D90`, EAX = map view);
- `0x00756170`, `0x0076B930`, `0x0076D340` (two sites) and `0x00777470`.

Which screens those last four functions are is **[?]**.

The terrain mode (`0x007FC980`) writes the same constant colour to every province. No other mode reads
country colours, except the sphere mode. **[C]**

`Country_GetMapColor` (`0x004FB5C0`) takes **ESI = country** and **EAX = the output `CColor`** (0x1C
bytes). It returns EAX unchanged, uses only ECX, EDX and XMM0, and ends with a plain `RET`. **[C]**

1. When `country+0xBD0` (the government, **[I]**) has an entry in the vector `country+0x15CC/+0x15D0`, it
   copies that colour. The entries are 0x20 bytes: a key compared with `[country+0xBD0]+0x40`, then a
   `CColor`. **[C]**
2. Otherwise it copies the base colour `country+0x924`, a `CColor` that is `color = { r g b }` of
   `common/countries/<name>.txt`. **[I]**
3. The vector holds the per-government lines of the same files, such as
   `democracy = { 155 78 75 }` in TGC's `Austria.txt`. **[I]**

The function has 19 call sites:

| site | function | what |
|---|---|---|
| `0x007FCAD7`, `0x007FCB22` | `MapColor_Political` | owner, controller |
| `0x007FCC36`, `0x007FCC76` | `MapColor_PoliticalPlain` | owner, controller |
| `0x007FFABF`, `0x007FFB05`, `0x007FFB74`, `0x007FFBB7` | `MapColor_Sphere` (`0x007FF880`) | a GP's own colour; a sphere member gets its leader's (`country+0x142C`) |
| `0x0051BB10`, `0x0051BB4F`, `0x0051BB8E` | `0x0051B870` | copies a text file and rewrites its `color` lines with the country's colour (r, g, b × 255) |
| `0x0060CEB5` | `0x0060CA30` | colours of the `debt_listbox` chart |
| `0x00653379`, `0x00653446`, `0x0065349C` | `0x00652A10` | game start/load **[?]** |
| `0x00688985`, `0x00688992` | `0x006881D0` | **[?]** |
| `0x0078636B`, `0x007867C7` | `0x00785400`, `0x00786780` | UI **[?]** |

A change confined to the map must therefore redirect the map sites, not the function itself: the file
rewrite at `0x0051B870` would otherwise write the changed colour.

`CColor_Scale` (`0x007EBB80`) takes ECX = source, EAX = output and XMM1 = factor. It returns r, g, b × factor,
clamped to 1, with the alpha copied. `+0x18` is set when all three components are above 0.999.
`CColor_ToARGB` does not read `+0x18`. **[C]**

**XMM1.** `MapColor_Political` keeps `k` in XMM1 across the owner's `Country_GetMapColor` call and passes
it straight to `CColor_Scale` (`0x007FCAE2`). A replacement for that call must keep XMM1. **[C]**

### Overlords, substates, puppets and spheres

The fields below are read from the triggers. Each is **[C]**.

| field | meaning | read by |
|---|---|---|
| `country+0xCF4` (u8) | is a substate | `is_substate` (`0x008D7760`) reads only this; `substate_of` requires it |
| `country+0xCF5` (u8) | is a vassal (puppet) that is not a substate | `is_vassal` (`0x008D7A40`) and `vassal_of` take `+0xCF5` or `+0xCF4` |
| `country+0xCF8/+0xCFC` | overlord {tag, index} | `substate_of` / `vassal_of` compare `+0xCFC` with the target's index |
| `country+0xDE0` (i32) | number of substates | `num_of_substates` (`0x008C1150`) |
| `country+0x1428/+0x142C` | sphere leader {tag, index} | `has_sphere_leader` (`+0x142C != 0`), `in_sphere` (`0x008F9C70`) |
| `country+0x1418/+0x141C` | a GP's sphere members, 8-byte entries {?, index} | `MapColor_Sphere` |

`structs.csv` used to call `+0xCF4`/`+0xCF5` `hasOverlord`/`isOverlord` and `+0x142C` `overlord`. Those
names were wrong and have been corrected.

**Built (2026-10-24).** The Script Extender's `vassal_map_color` redirects the four owner and controller
calls of the two political functions (`se_vassal_color_*` in `patch_sites.csv`) to a thunk:

1. The thunk runs `Country_GetMapColor`.
2. When `+0xCF4` or `+0xCF5` is set and `+0xCFC` names another country, it takes the overlord's colour from
   the same function. r, g and b become `overlord × (1 − own) + vassal × own`, where `own` is
   `vassal_map_color_own` / 100 (default 0.3).
3. It saves every register and XMM0-7 (`SE_SAVE_ALL`) around the C++ code, so XMM1 survives.

Because the overlord's colour comes from the same function, a per-government colour is included.
Occupation stripes take the same shade. The sphere mode, the file rewrite and the UI keep the real colour.
Not tested in game yet.

### Upload and redraw

`MapRenderer_UploadProvinceColors` (`0x00819830`, ECX = renderer, EAX = map view `+0x20D8`) calls
`MapRenderer_DrawProvinceColorTextures` (ESI = renderer). That draws the two 256×256 lookup textures:
province id `i` is the pixel `(i & 0xFF, 255 − (i >> 8))`. A selected province (`CProvince+0xC`, the
selectable interface at `+8`) is brightened by 0.1. **[C]**

Each frame, `CInGameIdler_ProcessMapDirty` (`0x00654530`, ESI = idler) reads:

- idler `+0x24`: pointer to `int[22]`, one dirty flag per mode;
- idler `+0x74` flags: `0x100` or the low nibble means recolour, `0x200` means upload only.

**[C]**

`CInGameIdler_MarkMapDirty` (`0x00648040`) takes EAX = idler, EDX = flags and the province id on the
stack. **[C]**

To force a redraw:

- **at once:** call `CInGameIdler_RefreshMapMode` (ECX = idler);
- **on the next frame:** set `(*(int**)(idler+0x24))[4] = 1`.

**[C]**

## Stripes texture

- The stripes come from `map/terrain/stripes.dds` (128×128 DXT1; vanilla has two diagonal stripes per
  tile, TGC four). `CProvinceManager_LoadTerrainTextures` stores its texture-manager index at
  `CProvinceManager+0xC0` once per map load. **[C]**
- `CProvinceManager_DrawTerrain` binds `textures[+0xC0]` as `tex7` (`StripesTexture` in
  `gfx/FX/terrain_2_0.fx`, Wrap, Linear, no mips) on every pass of every frame, whenever the map style
  (`CSettings+0xF0`) is not 0. The shader takes `lerp(primary, secondary, stripes.a)`. This is the only
  site that binds it. **[C]**
- The shader samples the stripes at world position / 8. One stripe per 128-texel tile is therefore the
  widest diagonal that still tiles without a seam. **[C]** shader, **[I]** geometry.
- Texture manager: `g_renderDevice+0x158`, also reachable as `CProvinceManager+0x3C`. It has 10 000
  entries of 0x2C bytes from `+0x15C`: `IDirect3DTexture9*`, path `std::string`, width, height,
  refcount. The device is at `+0x64`. `TextureManager_GetOrLoad` loads a texture once per path, as a
  MANAGED texture that survives a device reset. **[C]**
- Swapping the pointer in the stripes entry changes the stripes from the next frame on, with no code
  patch. The Script Extender does this while infrastructure is shown.

## Menubar tooltip

`CInGameIdler_BuildTooltip` finds the hovered `mapmode_<n>` button and calls `Localize` at `0x0070C5C8`
(`E8 13 C9 29 00`) with the key `MAPMODE_<n>`. **[C]**

`Localize` is stdcall `(out, const std::string* key)`, `RET 8`. A key that is missing from the table is
shown as the key text itself. **[C]** The table holds parsed text only, with no plain string to read back;
see [ui-console.md](ui-console.md#localisation-2026-10-23).

The Script Extender redirects this call: for `MAPMODE_6` it passes `"<MAPMODE_6 text> - <building text>"`
(texts read from the localisation CSVs), a key that is not in the table and is therefore shown as it is.

## Province tooltip and click

- **Tooltip:** `CProvince_GetTooltip` (`0x004AAFD0`, thiscall, ECX = `CProvince+8`, `(out, ctx)`,
  `RET 8`) is in the vtable slots `0x00DFD344` (`CMapProvince`) and `0x00DFD2B4` (`CProvince`). It
  switches on the map mode through the jump table at `0x004B1600`. The infrastructure entry
  `0x004B1610` (`46 E5 4A 00`) goes to `0x004AE546`, which appends the owner name and
  `INFRA_LEVEL_HERE` (VAL = `CProvince+0x2B8`). **[C]**
- **Click:** `CProvince_OnClick` (`0x004B17F0`) has nothing mode-specific. It only marks the map
  dirty with `0x200`. **[C]**

## Highlighting

Mechanisms that already exist:

- **Selection tint**, the `CProvince+0xC` flag. It is real selection state: do not borrow it.
- **Stripes** through the secondary colour. Infrastructure uses them for constructions.
- **Dimming** with the 0.3 factor for provinces the player has not seen.
- **Map-mode icons** through `FUN_00645A20` (recruitment, migration). **[I]**

Not traced: the "pick a province" modes. Idler `+0x70` together with a click redirect through
`CProvince+0x54` is a lead. **[?]**

## Hook sites

| purpose | site | original bytes | notes |
|---|---|---|---|
| catch the infrastructure button and shortcut | imm32 at `0x0064B152` | `B8 F0 C9 66 00` (instruction at `0x0064B151`) | write our handler's runtime address at init; it is called as `void __thiscall(CInGameIdler*)`; mode already 4 → cycle the building or open a selector, then chain to the original |
| alternative: detour the handler | `0x0066C9F0` | `55 8B EC 64 A1 00 00 00 00` | return address `0x00679AA2` = programmatic call |
| replace the infrastructure colours | `CALL` at `0x0066CABE`, or the entry `0x008014D0` | `E8 0D 4A 19 00` / `55 8B EC 6A FF 68 9B A8 B6 00` | stdcall, `RET 4`, one caller |
| tooltip | slots `0x00DFD344`, `0x00DFD2B4`, or jump-table entry `0x004B1610` | `D0 AF 4A 00` / `46 E5 4A 00` | post-hook and append to the `out` string |
| every mode change | entry of `MenuBar_SetMapModeButtons` `0x0066C180` | `55 8B EC 64 A1 00 00 00 00` | stdcall `(idler, button)`, `RET 8`; called by all 22 handlers and `Game_FinalizeLoadAndEnterGameplay` |
| redraw | call `0x00678A80` | `8B 81 B4 0D 00 00 E9` | ECX = idler; after a selection change and daily |

The Script Extender's `building_map_mode` uses the handler imm32, the colour `CALL`, the tooltip `Localize`
`CALL` (`0x0070C5C8`) and the `MenuBar_SetMapModeButtons` entry (rows `se_*` in `symbols/patch_sites.csv`):

- **Button.** A click while the mode is already 4 selects the next province building of
  `g_buildingDb+0x1C`; the first click selects the railroad.
- **Colours.** Only the player's own provinces are coloured, and for a `port` building only coastal ones;
  everything else is vanilla's khaki. A level under construction counts as built.
  - Solid blue: the level is at the highest one reachable now, `min(max_level, tech max − terrain
    reduction)`, the limit of `Building_CanBuildInProvince` ([buildings.md](buildings.md)).
  - Blue with khaki stripes: it can be expanded (controller = owner and `Building_CanBuildInProvince` with
    `ignoreConstructing`).
  - Red with khaki stripes: anything else.
- **Stripes.** A 128×128 texture with one wide diagonal stripe goes into the stripes entry of the texture
  manager while button 6 is shown, and the original goes back for any other button.
- **Tooltip.** As in [Menubar tooltip](#menubar-tooltip).

What has been tested in game is in `log.md` (2026-10-23, "Script Extender: building_map_mode").

## Open questions

1. What the red stripe of the infrastructure mode means (probably a construction of another building, see the slot-13 note above).
2. How the shader uses the stripe texture and `CSettings+0xEC/+0xF0`.
3. Who calls `CInGameIdler_PushMapMode` / `CInGameIdler_PopMapMode`.
4. Whether idler `+0x70` and `CProvince+0x54` are the "pick a province" mechanism.
5. Which glue slot is which event (the layout is known, see [The glue](#the-glue-cbuttonobservergluecingameidler)),
   and whether one of them is a right click (a selector on right click instead of cycling).
6. The tooltip `out` object beyond its string, and what `ctx` is.
7. Whether the minimap reads the same textures.
8. Whether the political mode recolours when a country becomes or stops being a vassal (the daily dirty flags), or only on the next mode change.
9. Which screens the other callers of `MapColor_PoliticalPlain` (`0x00756170`, `0x0076B930`, `0x0076D340`, `0x00777470`) are.
