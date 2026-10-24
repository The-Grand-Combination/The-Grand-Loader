# Events

Loading, the body keywords, the daily MTTH scan, firing, the event window (picture and background), and
the `country_event` / `province_event` effects.

Sources: `Engine/Event_System.md` and `Engine/Country_Daily_Update.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering)
(static reading, no debugger); the original bytes of every patch site below were re-checked against
3.04 in this repo. Profile numbers are ours. The body keywords, the window and the GUI sprite swap
(2026-10-24) are our own reading of the listing.

## Summary

- Events are loaded once into two vectors of the event manager (country / province events).
- The daily scan (`EventManager_DailyScan` `0x008A7CD0`, called from `DayTick` when `world+0xD11 == 0`)
  checks **countries on odd days and provinces on even days**: trigger first, then the MTTH, then a
  Mersenne Twister roll. It costs ~2 % of a day in 1836 (TGC).
- `Event_Fire` (`0x008A8120`) chooses an option for the AI (computing every `ai_chance` twice) or
  builds the window for the player, and always runs the immediate effects.

## Entry points

| function | role | |
|---|---|---|
| `EventManager_LoadResources` `0x008A67B0` | lists event files (base + mod, PhysFS), parses each | C |
| `EventFile_ReadHeader` `0x009A1440` | reads `country_event` `=` `{` | C |
| `Event_ctor` `0x008A5AC0` | 0x1C4-byte event object | C |
| `EventManager_DailyScan` `0x008A7CD0` | daily MTTH scan | C |
| `Event_ComputeMTTHProbability` `0x008AE380` | probability per million | C |
| `Event_Fire` `0x008A8120` | 17 callers (scan, effects, on_actions, delayed events, console) | C |
| `EventOption_EvaluateAIChance` `0x008AEB00` | option weight, pure | C |
| `Event_OnToken` `0x008A6280` | body keywords (`CEvent` vt slot 4, `+0x10`) | C |
| `EventWindow_ctor` `0x008A8F30` | player window; stdcall (this, event, scope by value), RET 0x50 | C |
| `Console_Event` `0x0041F170` | console `event <id> [tag]`: second caller of `EventWindow_ctor` | C |
| `EventEffect_ParseStatement` `0x008E7F80` / `EventEffect_Execute` `0x0088D370` | `country_event = { id days }` | C |
| `ObjectRegistry_FindByTypeAndId` `0x009C16D0` | events are type `0x27` in `g_registryLow` | C |

## Loading

`EventManager_LoadResources` takes the event manager: `+0x00` vector of events whose first token is
`0x3FF`, `+0x20` vector for `0x400` (very likely `country_event` / `province_event` **[I]**). Per file
it reads the header, allocates the event, pushes it and calls `event->vt[3]`, the generic block reader
(below). An event with an empty name, empty description or no options is logged (messages `0x9F`,
`0xA1`, `0xA4`) **and kept**.

## Body keywords

Every script object is read the same way. `CEvent` inherits the generic persistent object; its vtable
(`0x00E2C834`) has:

| slot | function | role |
|---|---|---|
| 3 (`+0x0C`) | `Persistent_ReadBlock` `0x009A2A90` | loop: stop at `}` (token type 4) or end of file (`0x13`); otherwise read `key = value` (`EventFile_ReadHeader`) and call slot 4 with (parser, key token). After the block: slot 5 when the parser has no errors, else it logs `Error: "<msg>" in file: "<file>" near line: <n>` and clears the error list |
| 4 (`+0x10`) | `Event_OnToken` `0x008A6280` | thiscall (ECX = event; parser, key token), RET 8: one `switch` on the key token |
| 5 (`+0x14`) | `0x004010C0` | after a clean block; an empty function for events |

Parser fields used by the handlers **[C]**: `+0x20` key token id, `+0x24` key text, `+0x228` value token
type (`0x29F` = `yes`, `3` = `{`), `+0x22C` value text (NUL-terminated), `+0x0C` error count, `+0x14`
error flag. `String_AssignFromParser` (`0x004012F0`: EDX = parser+0x22C, push destination
`std::string*`) copies the value into a field.

Token ids are those of `Tokens_RegisterKeywords` ([raw/script-tokens.tsv](../raw/script-tokens.tsv)):

| key | token | stored at | how |
|---|---|---|---|
| `id` | `0x0B` | `+0x08` (type 0x27, id) | `atol`, then `vt[6]` registers the event in `g_registryLow` |
| `title` | `0x3E2` | `+0xDC` string | |
| `desc` | `0x3E3` | `+0x114` string | |
| `picture` | `0x7DF` | `+0x184` string | file name without extension, see [Event window](#event-window) |
| `trigger` | `0x3B6` | `+0x30` block | embedded object, read with its own `vt[3]` |
| `mean_time_to_happen` | `0x3B7` | `+0xC0` block | |
| `option` | `0x3B9` | `+0xB0` vector | new 100-byte option (`Event_InitSubobject4C`), read, appended |
| `immediate` | `0x4AC` | `+0x4C` effect object | read through the vtable at `+0x4C`; `Event_Fire` runs `+0x74` (see Firing), so `+0x74` is inside it **[I]** |
| `is_triggered_only` | `0x713` | `+0x1A0` byte | `== yes` |
| `allow_multiple_instances` | `0x9A2` | `+0x1A1` byte | |
| `major` | `0x793` | `+0x1A2` byte | selects `Event_Major_Window` |
| `fire_only_once` | `0x651` | `+0x1A3` byte | |
| `election` | `0x7BD` | `+0x1A4` byte | selects `Event_Election_Window` |
| `news` | `0xA21` | `+0x1A5` byte | `FUN_009A1BB0` |
| `issue_group` | `0x935` | `+0x1A8` string | |
| `news_title` | `0xA25` | `+0xF8` string | |
| `news_desc_long` / `_medium` / `_short` | `0x9EE` / `0x9EF` / `0x9F0` | `+0x130` / `+0x14C` / `+0x168` strings | |

**Unknown key** (any other token, including a registered keyword that events do not use):
`Parser_UnexpectedToken` (`0x009A0F40`, EDI = parser) appends an error "Unexpected <key text>", sets
`+0x14`, and `Parser_SkipValue` (`0x009A0E90`) skips the value, a whole `{ }` block included. The event
is kept with its other fields; the block's error is printed to the log. **[C]**

The event object has no free space: the last field, `issue_group` (`+0x1A8`, 0x1C bytes), ends at the
object size 0x1C4.

## Daily scan

```text
days = (world.date - 0x029C55C0) / 24
odd days:  for every country from array index 1, with country+0x20 != 0 and provinces:
               candidates = country+0x08 .. +0x0C,  mode = 1
otherwise: for every owned province (prov+0x12C != 0):
               candidates = province+0x14 .. +0x18, mode = 2
for each candidate ev:
    if !ev.trigger.Evaluate(scope)                         continue   ; CAndTrigger, short-circuit
    p = Event_ComputeMTTHProbability(ev+0xC0, scope, mode)
    if MT() % 1_000_000 >= p                               continue
    if ev+0x1A3 and world+0xB64 already holds ev           continue   ; fire_only_once
    Event_Fire(ev, state, scope)
```

```text
Event_ComputeMTTHProbability(block, scope, mode):
    v = block.base                       ; x1000
    for each modifier m:  if m.Evaluate(scope): v = v * round(m.factor) / 1000   ; no early exit
    if v < 0 and base > 0:  return 1
    if v <= 1000:           return 1_000_000          ; MTTH <= 1 day, or no MTTH at all
    return clamp(mode * 1_000_000 / (v / 1000), 1, 1_000_000)
```

`mode` multiplies the probability. Country events are scanned every second day with mode 1, so their
average delay is ~2 × MTTH; province events are also scanned every second day but with mode 2, so
~1 × MTTH. Whether that asymmetry is intended is unknown.

Who fills the candidate vectors (and drops `is_triggered_only` events, which would otherwise fire as
soon as their trigger is true because they have no MTTH) is not known yet.

## Firing

1. With `fire_only_once` (`ev+0x1A3`) the event is prepended to the hash table at `world+0xB64`
   (`count++`, no duplicate check, no rehash).
2. AI country (entry 0 in `world+0xAEC` and `country+0x208 != 0`): `total = Σ ai_chance(option)`, a
   roll `% 100` over cumulative percentages, option 0 when the total is ~0. Each option's
   `ai_chance` is evaluated twice (once for the total, once in the selection loop). The choice is
   queued as a 0xB0-byte command (`EventOptionCommand_ctor` `0x00907D40`).
3. Player: builds the window (`EventWindow_ctor`), unless `allow_multiple_instances` (`ev+0x1A1`) is
   0 and the idler's `vt[0xF4](event)` returns true (very likely: a window of this event is already
   open **[I]**).
4. Always: `ev+0x74` immediate effects (`vt[0x2C]`).

`EventWindow_ctor` contains a dead loop (`0x008A9434..0x008A9562`) that walks every pop of every
province of the player's country and calls the pure `Pop_GetCountryWeight`, discarding the result.
Player window only; patch site `event_dead_pop_loop`.

## Event window

`EventWindow_ctor` is stdcall (this, event, `CEventScope` by value, 0x48 bytes), RET 0x50, and returns
`this` (0xD0 bytes from `operator_new`). Its two callers, `Event_Fire` (`0x008A8476`) and
`Console_Event` (`0x0041F54E`), pass the result to `Window_Show`. **[C]**

| field | content |
|---|---|
| `this+0x04` | the GUI window instance |
| `this+0x08` | `*(country + 0xCDC)` for the scope's country index: the GUI owner the window is created through **[I]** |
| `this+0x0C` | the event |
| `this+0x10` | the scope copy |

### Which window

```text
if ev+0x1A4 (election):      "Event_Election_Window"   (SubTitle, State_name, parliament and population lines)
else if ev+0x1A2 (major):    "Event_Major_Window"      (country_flag1, country_flag2)
else if scope+0x0C != 0:     "Event_Country_Window"
else:                        "Event_Province_Window"   (Province, event_separator_image)
window = owner->vt[0x34]()->vt[0x2C](&name, owner->vt[0x3C]())      ; owner = this+0x08
```

The window types are in `interface/event_<kind>_window.gui`. Each has `backGround = "Background"` and a
child `guiButtonType` named `Background` whose `quadTextureSprite` is `GFX_event_<kind>_bg`
(`interface/eventwindow.gfx`: `gfx/interface/event_<kind>_background.dds`, effect `buttonstate3.fx`).
**The background belongs to the window type: every event of one kind gets the same one, and nothing in
the event body selects it.** The constructor then finds the children by name with
`window->vt[0x44](const std::string*)` (returns the element or 0): `Title`, `Date`, `Description`, the
option buttons placed from `event_<kind>_option_start` / `_offset`, and `event_images`.

### Picture (`0x008AB12A..0x008AB4A1`)

```text
icon = window->vt[0x44]("event_images")
if !icon: log "Wrongly Trying to set event image <picture> for event window <window> for event <name>"
          (eventwindow.cpp) and stop
path = <gfx folder> + "/pictures/events/" + ev.picture (+0x184) + ".tga"
       ; g_searchPaths+0x4C == 2 (a mod is loaded): the mod's file when PHYSFS_exists, else the game's
tmpl   = SpriteTypes_Find(g_renderDevice + 0xA0, "GFX_event_no_image")
sprite = TexturedSprite_ctor(new 0x1AC, <device +0x6B9BC/+0x6B9C0>, path, tmpl, g_renderDevice)
icon->vt[0xE0](sprite)          ; SetSprite
icon->vt[0x34]()
```

So `picture` names a file, not a sprite. The template `GFX_event_no_image` provides the effect and the
other sprite settings; the texture comes from the file.

### GUI elements and their sprites

- **Types.** `.gui` files are read by `GuiTypes_CreateType` (`0x009A41C0`): `guiButtonType` (`0x7C`) →
  0x29C bytes (`GuiButtonType_ctor` `0x009F1D30`), `iconType` (`0xB3`) → 0xB0 (`IconType_ctor`
  `0x009ECA80`), `windowType` (`0x98`) → 0x2A4 (`WindowType_ctor` `0x009CD7C0`), `instantTextBoxType`
  (`0x147`) → 0xE4. The EU3 layer (`EU3GuiTypes_CreateType` `0x00646220`) makes `guiButtonType` a
  `CEU3ButtonType` (vtable `0x00E08D2C`). In a `windowType`, `backGround` (`0x99`) is a string at `+0x84`.
  These are definitions; windows are instantiated from them by name.
- **Runtime classes.** `CEU3Button` (vtable `0x00E08BEC`) and the base button vtable `0x00E3CE50` share
  slot `0xE0` = `Button_SetSprite` (`0x009EF5B0`, sprite at `+0xA0`). The icon vtables `0x00E3A930` /
  `0x00E0A834` have slot `0xE0` = `Icon_SetSprite` (`0x009C48D0`, sprite at `+0x94`). The icon case is the
  one the picture code uses; that the event window's `Background` is a `CEU3Button` is **[I]**.
- **SetSprite(new)** (both): does nothing for a null sprite; removes the old sprite from the render list
  and deletes it (`FUN_009963D0`: `old->vt[0x24](1)`); stores the new one; inserts it into the element's
  scene when the element is attached; copies the element's position (`vt[0xB0]` → sprite `vt[0x5C]`),
  state/frame (`vt[0x4C]` → `vt[0x60]`) and alpha (`vt[0x100]` or `+0xC4` → `vt[0x9C]`); and passes the
  element's size (`vt[0x3C]`) to `vt[0xC8]` **[I: the new sprite takes the element's size]**. The element
  owns the sprite from then on and deletes it with itself. **[C]**
- **Sprites by name.** `SpriteTypes_Find` (`0x005C2AD0`): EAX = the table (`g_renderDevice+0xA0` here),
  one stack argument `const std::string*`; hash `h = (h + c) * 61` over the name, chained buckets;
  returns the sprite type or 0. A new sprite of a type is `type->vt[0x48]()` (the idiom at `0x00405B5A`
  and seven more sites) **[C for those sites, I for every sprite kind]**.
- **Sprite from a file.** `TexturedSprite_ctor` (`0x009C4B50`, 19 callers: event pictures, news pictures
  `gfx/pictures/`, leader portraits `gfx/interface/leaders/*.dds`, technology images): (this, packed
  size from the device, `const std::string*` path, sprite-type template, device); the texture is loaded
  through the texture manager (`FUN_009BC490` on `device+0x158`).

## A `background` command: what it needs (2026-10-24)

The goal is to let an event's body choose the window background, for example
`background = "GFX_event_bg_war"`, for that event only. The findings above settle most of it:

1. **Parsing.** `background` is already a keyword, token `0x99` (`windowType`'s background, and the
   `define_general` field, read by another parser). In an event body it takes the unknown-key path, so
   without the plugin the event still loads, with one "Unexpected background" error in the log.
   Hook: replace `CEvent` vtable slot 4 (`0x00E2C844`, holds `0x008A6280`). For key token `0x99`, keep
   the value (parser `+0x22C`); otherwise call the original. `CEvent` has no free field, so keep the
   values in a side table keyed by the event pointer; events are never freed while the game runs **[I]**.
2. **When.** After `EventWindow_ctor` returns: `this+0x04` is the window, `this+0x0C` the event. Both
   call sites are `E8 rel32` (`0x008A8476`, `0x0041F54E`), so one wrapper with the same stdcall
   signature covers the player window and the console `event` command.
3. **Change.** `bg = window->vt[0x44]("Background")`, build a sprite, `bg->vt[0xE0](sprite)`. There are
   two ways to build the sprite:
   - a **sprite name** declared in any `interface/*.gfx`: `SpriteTypes_Find(device+0xA0, name)->vt[0x48]()`.
     The modder controls the texture, effect and frames as for any sprite. If the name is unknown, keep
     the default and log it. **Recommended.**
   - a **file**, like `picture`: `TexturedSprite_ctor` with the window's own `GFX_event_<kind>_bg` as the
     template and a path such as `gfx/pictures/events/<name>.dds`.
4. **Size.** SetSprite gives the new sprite the element's size **[I]**, so an image of another size is
   stretched to the background of the chosen window type. Election and major windows have their own
   sizes: an image made for the country window will look wrong there.
5. **Multiplayer and saves.** The change is display only; event files are already in the checksum, and
   nothing is saved. No risk of going out of sync.

**Built (2026-10-24):** the Script Extender's `event_background` (`script_extender/src/features/event_background.cpp`)
takes the **file** form, like `picture`: `background = "war"` shows `gfx/pictures/events/backgrounds/war.dds`
(or `.tga`), nothing to declare. The file is looked up as the picture is: `g_searchPaths` folder 2
(`gfx`) of the mod (`+0x244 + 2 * 0x1C`) and of the game (`+0x84 + 2 * 0x1C`), in the order of the mode
at `+0x4C`, with `PHYSFS_exists` (`0x00B21820`). The sprite is a `TexturedSprite` with the template
`GFX_event_country_bg` (the four event backgrounds differ only in their texture). `TextureManager_LoadFile`
(`0x009BC490`) reads through PhysFS and tries a `.tga` name as `.dds` first. The wrapper of the window constructor is a
naked thunk that pops the return address and calls the original with the caller's arguments in place: the
scope argument points to itself (`+0x24`), so a by-value C++ wrapper would hand the original a copy with a
stale pointer. It logs the `Background` element's vtable on every use, for the first check below.

To check in game before relying on it: that `window->vt[0x44]("Background")` returns the background
element (the window may hold it apart from its children, by its `backGround` property); that its slot
`0xE0` is `Button_SetSprite` (log the vtable); and that a `TexturedSprite` draws correctly in a button
that had a `quadTextureSprite`.

## Delayed events and `country_event = { id days }`

`EventEffect_Execute` looks the event up by `(0x27, id)` and fires it immediately when `days <= 0`,
otherwise schedules it on the country (`Country_ScheduleEvent` `0x00540A70`). Scheduled events live in
`country+0x68` / `province+0x384`; the countdown `item+0x54` is decremented once per call of
`Country_DailyUpdate` / `Province_DailyUpdate`.

## Event object (0x1C4 bytes)

See `CEvent` in [structs.csv](../symbols/structs.csv). Trigger at `+0x30`, immediate effects at
`+0x74`, options `+0xB0`, MTTH block `+0xC0` (base `+0x08`, modifiers `+0x0C/+0x10`, scale 1000),
strings and flags as in [Body keywords](#body-keywords).

## Patch sites

All in [patch_sites.csv](../symbols/patch_sites.csv); original bytes verified on 3.04.

| id | effect | status |
|---|---|---|
| `event_dead_pop_loop` | skip the dead pop loop in the player window | experimental (safe by construction) |
| `event_skip_fired_once_*` | test the fired-once table before the trigger, via code caves at `0x00C89120` / `0x00C89170` | no gain reported |
| `event_scan_every_4_days` + `event_scan_mode_*` | scan each scope every 4th day and double the probability | proposal; only worth it if events become a large share of the day (they are ~2 %) |

Code caves in `.text` padding: `0x00C8911A..0x00C891FF` (zeros, executable). Never share one cave
between two patches.

## Open questions

1. Who fills `country+0x08` and `province+0x14`, and where `is_triggered_only` events are filtered.
2. ~~The body parser and flags `+0x1A0`, `+0x1A2`, `+0x1A4`~~: answered 2026-10-24 (`is_triggered_only`,
   `major`, `election`; [Body keywords](#body-keywords)).
3. Bucket count of `world+0xB64` and of the object registries; whether they ever rehash.
4. The `background` checks listed at the end of [the command section](#a-background-command-what-it-needs-2026-10-24).
5. What `this+0x08` (`country+0xCDC`) is, and why the country window is chosen by the scope's country
   index rather than by `country_event` / `province_event`.
