# Performance: where the time goes and what to try

The working page for speeding the game up: the measured budget, the rules learned the hard way, and a
ranked list of candidates with their evidence. Update it whenever a measurement comes in.

Sources: `tick_profiler` logs in [raw/profiler/](../raw/profiler/), `year_timer` timings in
[docs/timings.md](../../timings.md), and reading `docs/re/decomp/` (see [ghidra.md](../ghidra.md)).

## Method

1. **Measure** with `year_timer` (ms/day of the first year, three runs per configuration, compare
   medians) and `tick_profiler` (inclusive/exclusive ms per block; levels 1–5).
2. **Read** the hot block in `docs/re/decomp/`. `index.tsv` gives every function's size, callers, callees
   and call depth from `DayTick` (`day`) and `CInGameIdler_Update` (`frame`). Virtual calls are not
   followed, so triggers and effects show `-1` although they run daily.
3. **Hypothesis -> feature behind an ini key, default off -> measure -> default on** only when the
   gain is above noise (~5 ms/day) and the results are unchanged.

## Rules learned

- **A hook cannot beat a call that costs tens of ns.** The decision cache, the v1 decision cull and
  the v1 CB cull all lost or broke even this way. Take the work out of the loop instead: v2/v3 replace
  the loop or the list it iterates.
- **Profile before patching.** "CBs are N×N" was true in shape and ~2 % in weight. Half of the real CB
  cost was `IsCBUsable`'s list walks and scope construction, not `can_use`.
- **Same results, same random numbers.** Anything that skips work which draws from the MT list changes
  the game's random sequence and desyncs multiplayer. Prefer changes that do the same work cheaper.
- **The clock itself costs ~100 ns.** Sample 1 call in 64 when timing hot code.

## Budget (TGC, 1836, max speed, ~190–210 ms/day)

| block | ms/day | share | status |
|---|---|---|---|
| frame side, outside `Update_World_and_Advance_Time` | ~90 | ~45 % | **split by level 4 (2026-10-11), see below** |
| ↳ `CommandQueue_Process` `0x009AF9F0` own | ~51 | 25 % | **solved**: ~46 of it is the ENet poll blocking in `select` (35 ms timeout); `skip_network_wait = 1` -> 155 ms/day ([network.md](network.md)) |
| ↳ `AI_FrameUpdate` `0x006874C0` | ~24–26 | 12 % | the daily AI decision loop (`AI_DecisionUpdate`); `cull_decision_checks` targets it |
| ↳ `Render_MapFrame` `0x006592F0` | ~14.5 | 7 % | 3–4 calls/day |
| ↳ other frame calls | ~5 | 2 % | `FUN_00654530`, `FUN_00657B60`, ... |
| `WorldMarketUpdate` `0x00484060` | ~23 incl. | 11 % | own 0.2; `Market_UpdateCountryPops` 14.8, pop cache misses: `prefetch_pops` (candidate 3) |
| `Country_DailyUpdate` own | ~20 | 10 % | decisions are **not** here (they run in `AI_FrameUpdate`) |
| `Country_PrePass` `0x00508590` | 27 | 13 % | split: `FUN_00515490` 8.7, own 6.5, `FUN_0050EA30` 3.3, `FUN_0051FD10` 3.0, `FUN_0050B9B0` 2.4 |
| `ProcessMarketAndGoodsDistribution` `0x004808D0` | 8.7 | 4 % | |
| AI CB scans | ~11 | 5 % | **done**: `cull_cb_checks` v3 (−11 ms) |
| decisions `potential` | (~23) | | inside `AI_FrameUpdate` above, not extra; **done**: `cull_decision_checks` v2 (−7 ms) |
| `FUN_0068F4C0` (1 call) | 4.4 | 2 % | |
| `Market_PostProcess`, events, provinces, `DayTick` own | ~11 | 5 % | |

## Candidates

Each candidate lists its evidence (**[C]** read in code, **[M]** measured, **[I]** inferred) and the
next step.

### 1. `CommandQueue_Process` (`0x009AF9F0`), ~51 ms/day own time: the network wait (done)

**Result (2026-10-11):** `sample_profiler` v3 put 45.8 ms/day in `ntdll!ZwWaitForSingleObject` under
`select` in ENet's `enet_host_service`, called every frame with a 15–35 ms timeout, also in single player.
`skip_network_wait = 1` took the first year from 195–203 to **155 ms/day (−20 to −22 %)**; the simulation
functions kept their times. See [network.md](network.md). The analysis below is how it was found.

Level 4 (2026-10-11, [raw/profiler/level4-2026-10-11.log](../raw/profiler/level4-2026-10-11.log)) split
the frame side. At max speed a game day is **3–4 frames** (~50 ms each), not one; rendering
(`Render_MapFrame`) is only ~14.5 ms/day. **[M]** The largest block is `CommandQueue_Process`: called
from five sites in `CInGameIdler_Update`, it runs the queued commands through virtual calls (`CALL EAX`
on `vt+0x1C`, `+0x20`, `+0x38`). The hourly-tick command, and therefore the whole simulation, is one of
them. It also loops `Entity_RegisterSpatial` + `FUN_005F2AE0` over a queue. **[C]** Its 51 ms of own
time are those unhooked virtual calls: probably the commands the AI issues (diplomacy, units,
construction) and their execution.

- Next: a **sampling profiler** (see [Tooling](#tooling)): call redirection cannot see virtual calls.
- Earlier idea, now weaker: "render fewer frames per day" would save at most part of the ~15 ms of
  rendering, not the ~90 ms.

### 2. `AI_FrameUpdate` (`0x006874C0`), ~24–26 ms/day

Per frame, it drives `AI_CountryFrameJob` and so `AI_DecisionUpdate`, the daily decision loop. With
`cull_decision_checks = 0` its time (~24 ms) matches the decision `potential` cost measured before
(~23 ms), so decisions are essentially all of it. **[M]** `cull_decision_checks` v2 already cuts ~7 ms
here; the remaining ~16 ms is the evaluation of decisions that can still pass.

**Sampled (v3, 1837, all fixes):** the job is 21.9 ms/day: `AI_DecisionUpdate` 10.7 and
`AI_CountryDailyJob_Update` (`0x0084B690`) 9.7. In the latter, `AI_TargetScan` (`0x0084B0F0`, 3.9 ms
own) stalls on relation `+0x34` for every country (90 % of its samples at `0x0084B29D`). **[M]**
Patch: `prefetch_ai_relations` (shipped 0, not measured). `FUN_00826480` (4.1) has no hot spot.

### `UpdateTimeThrottle` inside the tick: ruled out

`WorldMarketUpdate` calls it every 20 countries and the tick ~1400 times a day, and it can render a frame
in the middle of the market. Measured at level 4: **1.0 frame/day rendered from inside the tick, ~5
ms/day** (2.5 %). **[M]** Removing it is not worth the loss of responsiveness at slow speeds.

### 3. `WorldMarketUpdate`: the per-pop market loop (`prefetch_pops`)

**Sampled (v3, 1837, all fixes on, 154 ms/day):** `WorldMarketUpdate` is ~23 ms/day inclusive but only
0.2 ms of its own; the "21.6 ms exclusive" of the tick profiler was unhooked callees. **[M]** Most of it
is `Market_UpdateCountryPops` (`0x00485E40`, 14.8 ms): `FUN_004DD3C0` 5.9 (artisan/needs, through
`FUN_00485390` and `FUN_00483AA0`), `FUN_00485960` 2.4, own 2.0, `FUN_00484F90` 1.9, `FUN_00485390` 1.7.
The rest of the economy is a flat spread of small goods-vector primitives (`ScaledDotProduct`,
`Stockpile_SubtractArtisanFactory`, `FUN_0047E220`, 64-bit `__alldiv`/`__allmul` ~5 ms together, called
from everywhere).

Per instruction, the hottest samples sit right after the first read of a pop: `0x004853D7` (after
`[pop+0x1B4]`, 34 % of `FUN_00485390`), `0x004869F6` (after `[pop+0x68]`, 32 % of the loop's own time),
`0x00486A1C` (after `[pop+0x1D4]`, then `[that+0xB0]`). **[M]** The loop walks, for each province of the
country and each pop type of the market (`market+0x804`), a linked list (`province+0x194 + type*16`,
next = `pop+0x27C`) **[C]**: every pop is a dependent cache miss.

- **Built:** `prefetch_pops` (shipped 0): a register-preserving thunk on the call at `0x0048468F`
  collects every list head of the country, advances all lists together and prefetches each pop's
  `0x280` bytes and the `+0xB0`/`+0xE8` lines of `[pop+0x1D4]`. Read-only.
- **Next:** `year_timer` against 137 ms/day; a sampling run to see the stalls move. If it works, the
  same walk applies to the other daily per-pop loops (`Country_DailyUpdate`'s pop pass,
  `Economy_AccumulateElement` at `0x00969889`, a stall on the pop's `+0xA4` vector).
### 4. `Country_Recalculate` (`0x00515490`) (8.7 ms, 322 calls/day)

It resets dozens of `CCountry` fields (`+0x94`, `+0x1434`, `+0x1414`, vectors at `+0xD38..+0xD98`,
`+0xEA4`, `+0xEB4`, frees `+0xA28`/`+0xA38`) and recomputes them. It has 43 callers (UI, diplomacy,
events, load); per day it is reached from `Country_PrePass` (`0x005089DB`) and `Country_DailyUpdate`
(`0x0050B6CF`). **[C]** It frees and reallocates its vectors on every call.

- Next: check whether both daily call sites run for the same country on the same day (a recalculation
  done twice), and which inputs it really depends on.

### 5. `Country_DailyUpdate` own time (~20 ms)

Pops are already spread over the month. Remaining suspects: the O(n²) relation-purge loop (683 × 683
pairs, cheap per pair **[I]**), migration scoring and sorting, unit updates.

- Next: hook the large calls inside it (a "level 6", same machinery) and measure.

### 6. Allocation churn

The most-called functions reachable from the tick are allocator and string helpers:
`operator_delete` (`0x00AAE91B`, 33 509 call sites), `std_string_dtor` (`0x00408AD0`, 38 527), `std_string_assign` (`0x00409350`,
14 124). **[C]** for the call counts. These are static call sites, not calls per
day. **[I]**

- Next: count allocations per day (a hook on the CRT `malloc`/`free` imports with a sampled counter).
  If they are in the millions, a faster allocator could pay off without touching game logic: the
  2010-era CRT heap with low-fragmentation-heap tuning, or a size-class pool.

### 7. Parallelism (later)

The TBB scheduler is already initialised; only three `parallel_for` sites use it. The per-country loops
(`Country_PrePass`, `WorldMarketUpdate`) write to shared accumulators ([threads.md](threads.md)), so
they need per-thread accumulators and a merge in the original order to keep results bit-identical.
High effort; only after 1–4.

## Tooling

- `tick_profiler` redirects `CALL rel32` sites and vtable slots: exact call counts and inclusive times of
  known blocks, but blind to `CALL EAX` / `CALL [reg+n]`, which is where the largest remaining block lives.
- `sample_profiler` ([docs/sample-profiler.md](../../sample-profiler.md), 2026-10-11): about once a
  millisecond it suspends the game thread, reads `EIP` and the return addresses on its stack, resumes it.
  `docs/tools/scripts/annotate_samples.sh` maps them to functions with `docs/re/decomp/index.tsv`. It sees
  everything (virtual calls, render, driver and CRT time by module), adds no cost to hot loops and needs
  no site tables. Use one profiler at a time.

## Next steps

Moved to [../next-steps.md](../next-steps.md) (2026-10-11). The old list: the `CommandQueue_Process`
sampling (done, network wait), level 5 (superseded by sampling), allocation count (ntdll is ~5 ms/day
since `skip_network_wait`, not pursued), late game (still open, option 1 there).

## Hot-spot scan (2026-10-11, samples with culls and prefetches off)

Share of each function's samples on its hottest instruction. High share right after a load = cache
miss; the fix pattern is a read-only prefetch before the loop.

| function | ms/day excl. | top instr. | what stalls | status |
|---|---|---|---|---|
| `FUN_00522CF0` | 5.6 | 90 % `0x00522E27` | relation `+0x34` | `prefetch_relations` |
| `AI_TargetScan` `0x0084B0F0` | 3.9 | 90 % `0x0084B29D` | relation `+0x34` | `prefetch_ai_relations` |
| `CountryRelation_DailyPurge` | 5.6 | 71 % | relation `+0x50` | `prefetch_relations` |
| `Country_PrePass` | 3.6 | 70 % `0x00508DEE` | relation `+0x2C` | `prefetch_relations` (offset added) |
| `Country_Recalculate` | 7.8 | 46 % | relation `+0x20` | `prefetch_relations` |
| `FUN_004B9C60` | 1.3 | 89 % `0x004B9C9B` | pop `+0x27C` (DayTick pop pass) | `prefetch_pops` (2nd site) |
| `FUN_00485390`, `Market_UpdateCountryPops` | 4.5 | 32–34 % | first pop read | `prefetch_pops` |
| `FUN_0095EB00` | 1.4 | 90 % `0x0095EB63` | pop `+0x11C` (market distribution) | `prefetch_pops` (3rd site, next pop) |
| `FUN_0095A5F0` | 2.1 | 46 % `0x0095A66D` | pop `+0x180..+0x21C` | open |
| `FUN_0052FCD0` | 1.9 | 48 % `0x00530388` | `other+0x1580[self]` | `prefetch_relations` (4th site) |
| `FUN_009E6C10` | 2.2 | 67 % `0x009E6C2F` | flag trie node (`has_country_flag`) | open; mostly decision triggers, already culled |
| `Economy_AccumulateElement` | 2.4 | 55 % `0x00969889` | pop `+0xA4` vector | open |
