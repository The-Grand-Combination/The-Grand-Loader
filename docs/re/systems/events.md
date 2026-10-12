# Events

Loading, the daily MTTH scan, firing, and the `country_event` / `province_event` effects.

Sources: `Engine/Event_System.md` and `Engine/Country_Daily_Update.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering)
(static reading, no debugger); the original bytes of every patch site below were re-checked against
3.04 in this repo. Profile numbers are ours.

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
| `EventWindow_ctor` `0x008A8F30` | player window | C |
| `EventEffect_ParseStatement` `0x008E7F80` / `EventEffect_Execute` `0x0088D370` | `country_event = { id days }` | C |
| `ObjectRegistry_FindByTypeAndId` `0x009C16D0` | events are type `0x27` in `g_registryLow` | C |

## Loading

`EventManager_LoadResources` takes the event manager: `+0x00` vector of events whose first token is
`0x3FF`, `+0x20` vector for `0x400` (very likely `country_event` / `province_event` **[I]**). Per file
it reads the header, allocates the event, pushes it and calls `event->vt[3]` (body parser, not yet
analysed). An event with an empty name, empty description or no options is logged (messages `0x9F`,
`0xA1`, `0xA4`) **and kept**.

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
3. Player: builds the window (`EventWindow_ctor`), unless `ev+0x1A1 == 0` and the UI manager
   suppresses it.
4. Always: `ev+0x74` immediate effects (`vt[0x2C]`).

`EventWindow_ctor` contains a dead loop (`0x008A9434..0x008A9562`) that walks every pop of every
province of the player's country and calls the pure `Pop_GetCountryWeight`, discarding the result.
Player window only; patch site `event_dead_pop_loop`.

## Delayed events and `country_event = { id days }`

`EventEffect_Execute` looks the event up by `(0x27, id)` and fires it immediately when `days <= 0`,
otherwise schedules it on the country (`Country_ScheduleEvent` `0x00540A70`). Scheduled events live in
`country+0x68` / `province+0x384`; the countdown `item+0x54` is decremented once per call of
`Country_DailyUpdate` / `Province_DailyUpdate`.

## Event object (0x1C4 bytes)

See `CEvent` in [structs.csv](../symbols/structs.csv). Trigger at `+0x30`, immediate effects at
`+0x74`, options `+0xB0`, MTTH block `+0xC0` (base `+0x08`, modifiers `+0x0C/+0x10`, scale 1000),
flags `+0x1A0..+0x1A4`.

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
2. The body parser (`CEvent_vftable` slot 3) and the meaning of flags `+0x1A0`, `+0x1A2`, `+0x1A4`.
3. Bucket count of `world+0xB64` and of the object registries; whether they ever rehash.
