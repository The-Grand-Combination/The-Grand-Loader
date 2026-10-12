# `cull_decision_checks` / `decision_profiling`

Source: `patch/src/features/performance/decision_cull.cpp`. Measurements: [re/raw/profiler/](../re/raw/profiler/). Research: [re/systems/decisions.md](../re/systems/decisions.md).

## Why

Every day the AI loop (`AI_DiplomacyUpdate` `0x0085D270`) evaluates the `potential` of every decision for
every AI country, with no early exit; the player's alert (`FUN_005399C0`) does the same for the player
until the first available one. In TGC (1651 decisions, ~320 countries) that is ~529 k evaluations/day
costing 23.5 ms of a ~200 ms day at max speed (12 %). The cost is flat: the heaviest decision costs
0.29 ms/day, the top 25 together 2.5 ms. Reordering conditions in the scripts therefore buys little;
not evaluating does.

Three classes of evaluation are known to be false before they start:

| class | per day (TGC, 1836) | ms/day |
|---|---|---|
| decision with a top-level `tag = X`, evaluated for a country other than X | 213 000 | 6.5 |
| decision with a top-level `ai = no`, evaluated for an AI country (136 in TGC) | up to ~43 000 | ~1.3 |
| (country, decision) pair whose top-level `NOT = { has_*_flag }` gate already failed | 10 000, growing through the game | 0.45 |

## What did not work: a per-call hook

The first build intercepted every `potential` evaluation (vtable swap on the embedded `CAndTrigger`)
and returned false early for those classes. It saved the evaluations but paid a lookup, a scope read
and a few branches on all 529 k calls, about as much as the ~30 ns evaluations it skipped. Measured with
`year_timer`: 196 ms/day without, 203 with (noise). The lesson is general for this engine: a hook on a
call that costs tens of nanoseconds cannot win; the work has to disappear from the loop.

## Where the AI evaluates decisions

Not where the first write-up said. `AI_DiplomacyUpdate` (`0x0085D270`) does hold two decision loops,
through `DecisionManager_Get()` (`0x00875040`, called only at `0x0085D34F` and `0x0085D459`, `ESI` = AI
object with `+0x0C` country index), but it runs for a handful of countries a day (1-6 list requests/day
measured). The daily loop is `AI_DecisionUpdate` (`0x00821190`): once per AI country per day, driven
from the per-frame AI job (`CInGameIdler::Update` `0x00654D80` -> `FUN_006874C0` -> `FUN_0081E9E0` ->
`CALL` at `0x0081EA9B`, `ECX` = AI object, `+0x30` tag, `+0x34` country index), reading the global
`DAT_01319730` directly and testing `vt[6]() && potential && allow` before `ai_will_do`. That is the
1651 x ~320 = 529 k evaluations/day. The player's alert `FUN_005399C0` runs for the player only (the
call at `0x0050B792` is guarded by `country == world+0xB60`) and from the frame update.

## Mechanism: per-country lists

Both AI loops are given a private copy of the manager (0x38 bytes) whose two vectors point at that
country's filtered lists. For `AI_DecisionUpdate` the call at `0x0081EA9B` goes to a wrapper that
points the global itself at the copy for the duration of the call and restores it in a `__finally`;
for `AI_DiplomacyUpdate` the two getter calls go to a stub that passes `[ESI+0xC]` to the same
replacement. The loops then iterate fewer entries and do nothing else differently: same order, same
calls, and a decision that is left out would have evaluated to false.

A country's lists are built from the full manager lists by evaluating, with a `CEventScope` for that
country (ctor `0x008A8650`, tag/index copied from the `CCountry`), only the classified children:

- **tag lock**: a direct child `CTagTrigger` (`0x00E30C80`) with THIS/FROM flags (`+0x18/+0x19`) clear;
  its `+0x20` is the country index of the literal tag. Kept only for that country.
- **ai check**: a direct child `CIsAiTrigger` (`0x00E30EE8`, value `+0x1C`, reads the human-player table at
  `world+0xAEC`), evaluated with the game's own code.
- **flag gate**: a direct child `CNotTrigger` whose children are *all* `CHasCountryFlagTrigger`
  (`0x00E307E8`) or `CHasGlobalFlagTrigger` (`0x00E30820`) with THIS/FROM clear, evaluated with the game's
  own code. Mixed blocks such as `NOT = { has_country_flag = x  war = yes }` are not gates: their
  falseness is not permanent.

Building a list costs about 1291 gate evaluations (string map lookups) plus 1651 cheap checks, ~0.2 ms
per country. Lists are built lazily on the first AI request and rebuilt when:

- `clr_country_flag` ran with that country as scope, or `clr_global_flag` ran anywhere (hooks on slot 11
  of `CClrCountryFlagEffect::vftable` `0x00E2ACC0` → `0x00882A40` and `CClrGlobalFlagEffect::vftable`
  `0x00E2ADA0` → `0x00882BA0`; `Execute(scope, arg2)`, `RET 0x8`, reached only through the vtable). These
  are the only ways a dead pair comes back. The hook only marks the list dirty; it is rebuilt on the next
  AI request, never while the loop may be iterating it.
- the date jumped (a save was loaded): all lists dirty.
- 90 game days passed since the build: picks up pairs that died since (a flag set cannot bring a
  decision back, so it needs no hook; it only delays the saving until the next rebuild).
- the manager lists changed (new game, reload): everything is reclassified.

The player's alert, the politics view and the tooltips keep reading the real manager; `allow`,
`ai_will_do` and effects are never touched.

### Why the results are identical

`CAndTrigger::Evaluate` (`0x008B8260`) is a short-circuit AND over children without side effects. A
decision is left out of a country's list only when one of its direct children is false for that
country, and the engine would then have evaluated the whole `potential` to false and skipped it. The
only timing difference is the one introduced on purpose: a pair that dies by flag stays in the list
until the next rebuild (at most 90 days), which is the unoptimised behaviour.

## Profiling

`decision_profiling = 1` swaps the vtable pointer of every decision's `potential` for a private copy of
`CAndTrigger::vftable` whose `Evaluate` slot times the call (1 in 16 sampled, `QueryPerformanceCounter`).
Every 30 game days it rewrites `v2cp_decisions.log` next to `v2game.exe` from scratch with running
per-day averages since the decisions were loaded, so the file has one line per decision whatever the
length of the run:

```
#   ms/day  evals/day  ns/eval  passed/day  TFA  decision
    0.290        320      908           0  ---  pay_owed_steamers
```

`T` = tag-locked, `F` = has a flag gate, `A` = has a top-level `ai` check. With culling on, the header
also says how many decisions the AI loop saw per country and how many each build left out by tag, ai
and dead gate; `v2cp.log` gets one summary line per period for each of the two features.

## Limits

- Only direct children of `potential` are classified. `OR = { tag = A tag = B }` or a gate nested in an
  `AND` are evaluated normally.
- A pair that dies by flag is evaluated normally until the next rebuild (≤ 90 days).
- A flag cleared by any means other than the two effects is seen on the next load or within 90 days.
