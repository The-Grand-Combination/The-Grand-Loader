# `tick_profiler` — where the game day goes

Diagnostic feature of `performance_patch.dll`. Source: `patch/src/features/profilers/tick_profiler.cpp`.

## What it measures

The daily tick of 3.04 as mapped in [docs/re/systems/tick.md](re/systems/tick.md):

| Block | Function | Redirected call site | Runs |
|---|---|---|---|
| `world_update` | `Update_World_and_Advance_Time` `0x00682EC0` | `0x0072ECFA` | once per game day |
| `day_tick` | `0x006859C0` | `0x00683AB1` | once per day, inside `world_update` |
| `provinces` | `Province_DailyUpdate` `0x004B1A10` | `0x00685B49` | per province |
| `countries` | `Country_DailyUpdate` `0x005091A0` | `0x00685ED1` | per country |
| `ai` | `AI_DailyUpdate` `0x0085CCB0` | `0x0050B0EB` | per AI country, inside `countries` |
| `country_ai2` | `FUN_0085CF10` | `0x0050B0F0` | per country, inside `countries` |
| `country_post` | `FUN_00519F80` | `0x0050B0F6` | per country, inside `countries` |
| `events` | `EventManager_DailyScan` `0x008A7CD0` | `0x006861E8` | once per day, inside `day_tick` |

Each call site has exactly one caller in the tick, so each is redirected to a thunk. The thunk notes
the time, swaps the return address for a stub that notes the end, and jumps to the original function
with the registers and the stack exactly as the caller left them. That works for any calling
convention the game uses (arguments in `EAX`, `ECX`, `EDI`, `XMM0`, `RET n`), because the profiler
never looks at the arguments. All registers, flags and `XMM0-7` are saved around the bookkeeping.

A shadow stack of frames gives *inclusive* time (the block and everything it calls) and *exclusive*
time (the block minus its hooked children). If a frame is lost to an exception unwind, the outermost
block resynchronises and the report says so.

## Levels

`tick_profiler = 1` hooks the eight blocks above. `tick_profiler = 2` additionally hooks every
game-code call inside `DayTick` (65 sites, 40 targets, generated into `tick_profiler_sites.h` from
the Ghidra listing; CRT, allocator and string helpers excluded). Unknown targets are reported as
`f_<address>`; name them in `kKnown` once identified.

## First result (2026-10-02, level 1, ~185 ms/day)

| Block | excl ms/day | share |
|---|---|---|
| outside `world_update` (render, UI, waiting) | ~80 | 43 % |
| `day_tick` exclusive | ~68 | 37 % |
| `countries` exclusive | ~19 | 10 % |
| `world_update` exclusive | ~7 | 4 % |
| `events` | ~3 | 2 % |
| `provinces` | ~2 | 1 % |
| AI (inside `country_ai2`; the `ai` site is not the one taken) | 0.1–8 | ≤4 % |

`Country_DailyUpdate` runs 683×/day for ~320 existing countries: the loop visits every tag defined
by the mod. Level 2 was added to split the 68 ms of `day_tick`.

## Report

Every 30 game days:

```
tick_profiler: days A-B, 30 days, wall 242.0 ms/day; outside world_update 48.1 ms/day (render, UI, idle)
tick_profiler:   block            incl ms   excl ms  calls/day
tick_profiler:   world_update      193.9      8.2          1
tick_profiler:   day_tick          185.7     22.4          1
tick_profiler:   provinces          61.3     61.3       2970
tick_profiler:   countries          90.1     31.5        318
tick_profiler:   ai                 40.3     40.3        300
...
```

How to read it:

- `wall` is real time per game day at the speed you ran; `outside world_update` is everything that
  is not the simulation step (rendering, UI, waiting for the speed throttle). At speed 5 it should be
  small; at lower speeds it is mostly idle.
- `excl ms` is where the time is actually spent. `day_tick` exclusive is the part of the daily tick
  not covered by the province/country/event blocks (economy, markets, pops aggregation, ...).
- `countries` exclusive is `Country_DailyUpdate` minus `ai`, `country_ai2` and `country_post`.

The biggest `excl ms` is the next thing to split further.

## Cost

A few thousand timer reads per game day (one pair per province and per country), well under 1 ms/day.
Still a diagnostic: leave it off when not investigating.
