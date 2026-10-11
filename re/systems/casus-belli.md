# Casus belli evaluation

How `cb_types.txt` is loaded and when `can_use` runs. Feature built on it:
[docs/cb-cull.md](../../docs/cb-cull.md).

Sources: this repo's sessions (2026-10-01..04). This page started as
`Engine/Casus_Belli_Evaluation.md`, written in these sessions for the maxioten repo, and is
maintained here now.

## Summary

- Daily, every AI country scans every other country twice (`AI_ScoreWarTargets`,
  `AI_ScoreDiplomaticTargets`) through `CBManager_IsAnyCBUsable`, which walks every CB type.
- A type's `can_use` runs for a pair only if the type has `always = yes` **or** the attacker already
  holds a CB of that type against that target. Everything else stops before any trigger runs.
- `CanUse` is `can_use.Evaluate(scope)` with no cache. Scope country = attacker, THIS = defender,
  FROM unset.
- Measured (1836, TGC): the AI scans cost ~11 ms/day, only 4.8 ms of it in `can_use`. The rest is
  `IsCBUsable` walking the held-CB list and building two scopes for every type. `cull_cb_checks` v3
  removes almost all 11 ms.

## Functions

| function | | |
|---|---|---|
| `CBManager_Get` `0x0090CC40` | lazy singleton `g_cbManager`; `+0xC/+0x10` = `vector<CCasusBelliType*>` | C |
| `CBManager_Load` `0x0090CDC0` | parses `common/cb_types.txt` (`peace_order` handled separately) | C |
| `CBManager_FindByName` `0x0090DA40` | hash `h = (h + c) * 61` | C |
| `CBManager_IsAnyCBUsable` `0x0092EDE0` | `(rel, defRef, attRef)`, stdcall: `for type: if IsRealType && IsCBUsable: return true` | C |
| `CBManager_IsCBUsable` `0x0092EEC0` | type index in `EDI`; ownership/`always` gate, two scopes, `CanUse` | C |
| `CCasusBelliType` vtable `0x00E33724` | slot 4 `OnKeyword` `0x0090A1E0`, 6 `IsValid` `0x0090A7E0`, 7 `IsRealType` `0x00A31490` (1), 8 `CanUse` `0x0090A7F0` | C |
| `CNullCasusBelliType` vtable `0x00E3374C` | `IsRealType` = `0x0058CFD0` (0) | C |
| `Console_Execute` `0x00420EB0` | `debug cb_use <type> <TAG> <TAG>` prints `can_use.Describe`; `cb_countries`, `cb_state` | C |

Field map of `CCasusBelliType`: [structs.csv](../symbols/structs.csv). Keyword ids:
[raw/cb-keyword-map.txt](../raw/cb-keyword-map.txt).

## `IsCBUsable` per pair **[C]**

```c
bool IsCBUsable(relation, defenderRef, attackerRef)        // type index in EDI
{
    for (n = relation->heldCBs /* +0x50 */; n; n = n->next /* +8 */)
        if (n->cb /* +0 */ ->typeIndex /* +0x24 */ == idx) goto evaluate;
    if (!type->always /* +0x132 */) return false;          // 0x0092EF63
evaluate:
    CEventScope outer(0), inner(0);
    outer.country = attackerRef; inner.country = defenderRef;
    outer.thisScope = &inner;                              // +0x24
    return type->CanUse(&outer);                           // vt +0x20
}
```

`relation` is `defender+0xBE8[attackerIndex]` at the AI call sites. `is_valid`, `allowed_*`, `months`
and `is_triggered_only` are not consulted. `is_triggered_only` does not affect the cost; `always` does.

## Who calls it

```text
Country_DailyUpdate -> AI_DailyUpdate (0x0085CCB0)
   gates: has provinces, tag != REB, country+0x37C == 0, +0x37D == 0, FUN_0050E450() != 0
   AI_DiplomacyUpdate (0x0085D270), if ai+0x19 == 0
     AI_ScoreWarTargets (0x0085DB40)        per D: self+0xE4C <= D+0xE4C * 5, strength >= g_aiWarStrengthRatio
                                            -> IsAnyCBUsable at 0x0085DCDA
     AI_ScoreDiplomaticTargets (0x0085E0E0) per D not overlord/vassal -> IsAnyCBUsable at 0x0085E5FB
```

Other callers, lower frequency: monthly from `Update_World_and_Advance_Time` (`0x00683B36`, through
`FUN_00687E30 -> FUN_0050C2A0 -> FUN_005183F0`); AI war planning (`AI_EvaluateCommandChance`,
`AI_GetOpinionScore`, `FUN_00844210`, ...); diplomacy UI validators; the script trigger
`casus_belli = X` (`CCasusBelliTrigger_Evaluate` `0x008D28C0`), which runs every `always` CB's
`can_use` each time the event/decision is checked.

## Modding consequence

Top-level `can_use` conditions short-circuit left to right with the attacker as scope. Put cheap,
attacker-only conditions (`tag`, `is_greater_power`, flags) first. Only the `always = yes` types pay
per pair (32 in TGC), so trigger order matters less than the overhead of `IsCBUsable` itself.

## Open questions

1. Meaning of `country+0x37C/+0x37D`, `ai+0x19`, `country+0xE4C`, `g_aiWarStrengthRatio`.
2. Order of the seven ×1000 factors at `+0xF4..+0x10C`.
3. Whether `CNullCasusBelliType` instances are in the manager vector.
