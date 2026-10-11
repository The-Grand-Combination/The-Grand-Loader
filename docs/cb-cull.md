# `cull_cb_checks` — casus belli check culling

Feature of `community_patch.dll`. Source: `patch/src/features/cb_cull.cpp`, class table
`patch/src/features/cb_cull_triggers.h`. Research: [re/systems/casus-belli.md](../re/systems/casus-belli.md).

## The problem

Every day, for every AI-controlled country, the AI runs two scans over every other country on the
map (`AI_ScoreWarTargets` `0x0085DB40`, `AI_ScoreDiplomaticTargets` `0x0085E0E0`). For each target it
asks `CBManager_IsAnyCBUsable` (`0x0092EDE0`), which walks every CB type and, for the types with
`always = yes` or already held against that target, evaluates the whole `can_use` block through
`CCasusBelliType::CanUse` (`0x0090A7F0`). There is no cache, so a condition that only depends on the
attacker (`tag = ENG`, `civilized = yes`, `has_country_flag = x`) is re-evaluated for every target.

## What the patch does

1. The two `CALL IsAnyCBUsable` sites in the AI scans (`0x0085DCDA`, `0x0085E5FB`) are redirected to
   a replacement of that loop. Nothing else in the game is touched; the diplomacy screen, the console,
   the `casus_belli` script trigger and AI war planning keep calling the original.
2. Once per (attacker, game day), the replacement builds the list of *live* CB types: for every type it
   evaluates only the attacker-only direct children of `can_use` (pre-classified once per type when
   the CB list is first seen) in a scope whose country is the attacker. A type with a false
   attacker-only child is dead for that attacker for the rest of the day.
3. For every defender, the loop then calls the original `IsCBUsable` (ownership / `always` gate plus
   the full `can_use`) **only for the live types**, in the original order, stopping at the first hit
   like the original. Dead types cost nothing per defender: no call, no ownership walk.

The first version hooked `CCasusBelliType::CanUse` instead, which still ran once per defender and
spent as much in the hook as the ~50 ns evaluation it skipped; it was replaced by the loop above.

## What counts as attacker-only

A trigger object is attacker-only when all of the following hold:

- its vtable is in the generated table (`cb_cull_triggers.h`): 174 *scalar* classes whose
  `Evaluate` reads only the scope's own country, literals and global state, plus 20
  *country-reference* classes (`tag`, `war_with`, `neighbour`, `alliance_with`, `truce_with`,
  `vassal_of`, `in_sphere`, ...) that compare the scope country against a literal tag;
- its `THIS` flag (`+0x18`) and `FROM` flag (`+0x19`) are both clear, i.e. the script wrote a
  literal, not `= THIS` / `= FROM`;
- for `AND` / `OR` / `NOT` blocks: every child is attacker-only (the block is then evaluated as one
  unit).

Everything else — scope changes (`FROM = { }`, `THIS = { }`, `any_*`, `all_*`, `owner`,
`capital_scope`, ...), `relation = { who = THIS }`, `is_core = THIS`, and the 40 classes that
dereference the THIS scope or evaluate children — is treated as pair-dependent and left to the
original evaluation. Being conservative here only costs performance, never correctness.

How the table was built: every `*Trigger::vftable` symbol recovered by Ghidra's RTTI analysis
(236 classes) had its `Evaluate` (vtable slot 6) decompiled and grepped for `scope+0x24`
(THIS scope), `scope+0x30/+0x34` (FROM), child evaluation and `CEventScope` construction. Classes
with none of those are scalar; classes whose value parser is `0x008D3F50` are country-reference.
The generic int/float/bool parsers also accept `THIS`/`FROM` and set the same flags, which is why
the flags are checked for every class.

## Scope semantics it relies on

In `can_use` the current scope is the **attacker** and the THIS scope (`scope+0x24`) is the
**defender**; `FROM` is unset. This is what `CBManager_IsCBUsable` (`0x0092EEC0`) builds:
`scopeA.country = attackerRef; scopeA.this = &scopeB; scopeB.country = defenderRef`.

## Correctness notes

- Triggers have no side effects, so caching a false verdict for the day is sound as long as the
  attacker's own state does not change between targets inside one scan. AI decisions taken during
  the scan are queued as diplomatic actions, not applied immediately.
- The cache is invalidated by the game day (`world+0xB0C / 24`), so loading a save or changing
  speed needs no special handling.
- Every patched location is byte-checked first; on any mismatch the feature logs and stays off.

## Log

`cull_cb_profiling = 1` adds counters and a sampled clock (1 call in 64) and writes, every 30 game
days:

```
cb_cull: days A-B (30 days, 240.1 ms/day wall): AI CB scans 1.9 ms/day over 1480 scans/day; 9400 type checks/day made, 65300 skipped (87%); pre-pass 0.3 ms/day for 290 attacker-days
cb_cull:   gendarme_of_europe           skipped 1410 checks/day
```

- `ms/day wall` is the whole game; `AI CB scans` is the time inside the replaced loop (ownership
  gates + `can_use` of the live types); `pre-pass` is the once-a-day attacker-only evaluation.
- `skipped` counts the `IsCBUsable` calls the original loop would have made for dead types; the
  per-type lines name the CBs that benefit most.
- Leave profiling off when not investigating; with it off the hook keeps no counters at all.

## v3 (2026-10-04): one scope pair per target, CanUse only where it can matter

Reading `IsCBUsable` (`0x0092EEC0`) showed where the per-pair cost really was: for **every** CB type it
walks the list of CBs the attacker holds against the defender (`relation+0x50`, nodes `+0` CCasusBelli,
`+8` next, CB `+0x24` = type index), returns false if the type is not held and not `always` (`+0x132`),
and otherwise builds two `CEventScope`s (attacker; `+0x24` -> THIS = defender) before `CanUse`
(vtable slot 8, `+0x20`). With ~100 types and ~177 k `can_use` calls a day that is ~100 list walks per
pair and ~350 k scope constructions a day, on top of the `can_use` blocks themselves.

The v2 pre-pass stays (per attacker and day: which types fail an attacker-only condition). Per
(attacker, defender) the replacement now:

1. builds the two scopes once, exactly as `IsCBUsable` lays them out;
2. calls `CanUse` for the live `always` types of the attacker;
3. walks the held list once and calls `CanUse` for each held type that is real, not `always` (already
   covered) and not dead today;
4. returns at the first true.

The original is a boolean OR over the types in manager order with no side effects, so the order change
does not alter the result; a type skipped here is one whose `can_use` would have been false (dead) or
that is neither held nor `always` (the original's own early exit).

Expected: most of the ~6 ms/day the 2026-10-02 profile left outside `can_use` plus the culled part of
the 4.8 ms of `can_use` itself. Measured with `year_timer`: see the research log.
