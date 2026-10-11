# Frame, hourly tick and daily tick

How the game turns wall time into game days, and what runs once per day. The timing numbers are from
`tick_profiler` on TGC in 1836 at max speed ([docs/tick-profiler.md](../../docs/tick-profiler.md)).

Sources: this repo's sessions (October 2026); the call-chain and `Country_DailyUpdate` notes in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering)
(`Engine/engine-structure.md`, `Engine/Country_Daily_Update.md`), re-checked against 3.04 where marked.

## Summary

- One frame = `CInGameIdler_Update`. Inside it, `ProcessGameTimeAdvance` decides whether a game hour
  passes (see [game-speed.md](game-speed.md)); each hour is a queued `CHourlyTickCommand`.
- `Update_World_and_Advance_Time` adds 24 to the date on every call, so a "game hour" command
  actually advances a whole day, and calls `DayTick` once. **[C]**
- `DayTick` is the daily simulation: provinces, then countries, then the event scan. The old name
  `Economy_ProcessMonthlyTick` is wrong. It runs once per day. **[C]**
- In 1836, ~185–196 ms/day are spent like this: ~43 % outside the simulation (render/UI), world
  market ~37 ms, country pre-pass ~27 ms, `Country_DailyUpdate` itself ~21 ms, AI ≤4 %, events 2 %,
  provinces 1 %.

## Call chain

```text
CInGameIdler_Update (0x00654D80, vtable slot 0x00E0A350)        per frame
  ProcessGameTimeAdvance (0x00682BD0)                             speed table, world+0xB14
  ... queue -> CHourlyTickCommand_Execute (0x0072ECF0, vtable 0x00E12414 slot 6)
        Update_World_and_Advance_Time (0x00682EC0)                <- 0x0072ECFA
          0x00682FA5  ADD [world+0xB0C], 0x18                      date += 1 day
          DayTick (0x006859C0)                                     <- 0x00683AB1
            per province: Province_DailyUpdate (0x004B1A10)       <- 0x00685B49
            1000 Mersenne Twister draws                            0x00685B6B
            per country from index 1: Country_DailyUpdate (0x005091A0)   <- 0x00685ED1
                 AI_DailyUpdate (0x0085CCB0)                       <- 0x0050B0EB
            EventManager_DailyScan (0x008A7CD0) if world+0xD11 == 0       <- 0x006861E8
          monthly: FUN_00687E30 -> ... -> CBManager_IsAnyCBUsable  0x00683B36 (month index changed)
  AI_CountryFrameJob (0x0081E9E0) via FUN_006874C0                per frame, spreads the AI over days
```

`UpdateTimeThrottle` (`0x00685620`) is called 1375 times a day from inside the tick and can render a
whole frame (`App_ProcessMessagePumpAndUpdate` `0x009DF2B0`) when the per-speed threshold is reached,
so the tick is re-entered from the frame. The profiler matches frames by `ESP` for that reason.

## Calendar

`world+0xB0C` is the date in hours. Base `0x029C55C0` = 43 800 000 h = year −5000. Days =
`(date − base) / 24`, year = `days / 365 − 5000`; no leap years. Month lengths are the table at
`kMonthLengths` (`0x00F1027C`). **[C]**

## `Country_DailyUpdate` (0x005091A0)

`(country, random array)`, `RET 8`, 683 calls/day in TGC (all defined tags; the loop skips countries
without provinces). In order: **[C]** unless marked

1. Pops of every owned province: a pop is updated when `pop.id % month_length == day_of_month`
   (`Pop_MonthlyUpdate` `0x00957EA0`), so each pop is updated once a month, spread over the days. A pop
   at size 0 is merged into another of the same type (`Pop_MergeEmpty` `0x004B9250`). **[I]**
2. Player-only statistics. **[?]**
3. Migration: score provinces, `std_sort`, distribute (`Migration_Distribute` `0x004BA430`). **[I]**
4. Leader list (`country+0xBD4`): daily death-by-age roll. **[I]**
5. Military lists `+0x7B4`, `+0x149C`, `+0x148C`. **[?]**
6. O(n²) loop at `0x0050ABF0`: for every other country with provinces,
   `CountryRelation_DailyPurge(country+0xBE8[other])` (`0x0092E290`), which purges five relation lists;
   empty lists are skipped, so an idle pair costs a few null checks.
7. Goods, prices, units, rank; `AI_DailyUpdate`; the player's decision alert (`0x0050B792`, player
   only).
8. Events: `on_action` pulses (`0x0050B1F4`) and delayed events (`country+0x68`, counter `+0x54`
   decremented per call, `0x0050B5CE`).

Removing the call entirely (`ADD ESP,8` in place of the `CALL` at `0x00685ED1`) was reported to speed
the game up visibly and to break army movement. Running it every other day is possible (patch site
`country_update_every_other_day`) but halves pop updates, doubles event delays and halves daily rolls;
the profile shows it is ~10 % of the day, so it is not worth it.

## Measured cost (1836, max speed)

| block | ms/day | note |
|---|---|---|
| outside `Update_World_and_Advance_Time` | ~90 | rest of `CInGameIdler_Update`: render, UI, AI frame jobs |
| `Economy_Update` (`0x00489990`) | ~37 | see [economy.md](economy.md) |
| `Country_PrePass` (`0x00508590`) | ~27 | 683 calls; `FUN_00515490` 8.7, `FUN_0050EA30` 3.3, `FUN_0051FD10` 3.0, `FUN_0050B9B0` 2.4 |
| `Country_DailyUpdate` own | ~21 | includes ~10–12 ms of decision `potential` before `cull_decision_checks` |
| AI CB scans | ~11 | removed by `cull_cb_checks` v3 |
| `DayTick` own | 2.7 | |
| events | ~4 | |
| provinces | ~2 | |

`Entity_RegisterSpatial` (`0x0068F390`) is called 19 047 times a day.

## Randomness

The game draws from a 624-word Mersenne Twister list (`g_mtState` `0x00F0ECF0`, index `g_mtIndex`
`0x00F0F6B0`); `Rng_Next` (`0x009B7610`) takes the next value and `Rng_RegenerateMT` (`0x009B7700`)
refills the list when the index wraps. `DayTick` draws 1000 values per day. Any patch that skips work
which draws random numbers changes the sequence, so all players of a multiplayer game must run it.

## Open questions

1. What `FUN_00515490` (8.7 ms/day, 322 calls) does.
2. The ~90 ms/day outside the simulation at max speed: how much is render, how much AI frame jobs.
3. Semantics of the `Country_DailyUpdate` military blocks.

## Frames per day: the AI job queue

At max speed a day takes several frames, not one. **[C]**

- At the end of `DayTick`, every AI object (`world+0xA4`, `+0x30` tag, `+0x34` country index) is appended
  to bucket `country % N` of the job queue at `world+0xB84` (16-byte list headers, `+8` = job count;
  N = 1 in single player, presumably one bucket per player in multiplayer).
- Every frame `CInGameIdler_Update` calls `AI_FrameUpdate` (`0x006874C0`, stdcall, at `0x006557C4`)
  when the first bucket is not empty. It pops jobs and runs `AI_CountryFrameJob` (decisions and the rest
  of the per-country AI) until the bucket is empty **or 0.02 s have passed** (double at `0x00E456C8`).
- `ProcessGameTimeAdvance` (`0x00682BD0`) issues the next `CHourlyTickCommand` only when every bucket is
  empty (and the speed accumulator passed its threshold, and this client is the host). The command goes
  through the session and is executed by `CommandQueue_Process` in a later frame.

So a day is: a frame that executes the tick (and starts the AI), more frames while the AI runs 20 ms at a
time, a frame in which the next tick is issued. Every one of them renders. The 20 ms budget is wall
time, so vanilla's frame boundaries (and which commands run between AI jobs) already depend on machine
speed. Patch: `fast_ai_queue` (finish the queue in one frame at max speed): first year 133 vs 137 ms/day,
one run, within noise (2026-10-11).
