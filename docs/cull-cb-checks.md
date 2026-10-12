# `cull_cb_checks`

## Source

`patch/src/features/performance/cb_cull.cpp`, class table `patch/src/features/performance/cb_cull_triggers.h`.
Research: [re/systems/casus-belli.md](re/systems/casus-belli.md).

## Why

Every day every AI country scans every other country twice (`AI_ScoreWarTargets` `0x0085DB40`,
`AI_ScoreDiplomaticTargets` `0x0085E0E0`) through `CBManager_IsAnyCBUsable` (`0x0092EDE0`), which walks every
CB type. For each type, `IsCBUsable` walks the CBs the attacker holds against the target, builds two
scopes and, for `always = yes` or held types, evaluates the whole `can_use`. Conditions that depend only
on the attacker (`tag = X`, `civilized = yes`, flags) are re-evaluated for every target.

## How

- The two AI call sites (`0x0085DCDA`, `0x0085E5FB`) go to a replacement of the loop; the UI, the console,
  the `casus_belli` trigger and war planning keep calling the original.
- Once per attacker and day, the attacker-only direct children of each type's `can_use` are evaluated; a
  type with a false one is dead for that attacker for the day. Attacker-only = a trigger class that only
  reads the scope country, literals or globals, or compares the scope country with a literal tag (194 of
  236 classes, from decompiling every `Evaluate`), with no `THIS`/`FROM` value; `AND`/`OR`/`NOT` blocks of
  such children count too.
- Per target, the two scopes are built once (attacker; `+0x24` -> THIS = defender), then `CanUse` runs for
  the live `always` types and for the held types that are live, stopping at the first true.
- The original is an OR over the types with no side effects, so skipping dead types, and the order
  change, give the same answer.
