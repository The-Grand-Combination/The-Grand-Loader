# Casus belli evaluation

How `cb_types.txt` is loaded, when `can_use` runs, and how a war goal's peace options are carried out. Features built
on it: [docs/cull-cb-checks.md](../../cull-cb-checks.md); Script Extender `demand_province` ("po_demand_province").

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

## Peace options (2026-10-24)

Sources: this repo's session of 2026-10-24 (decompilation and disassembly of the functions below).

### Storage **[C]**

`CCasusBelliType_OnKeyword` handles every `po_*` keyword the same way. The value must be `yes`
(`parser+0x228 == 0x29F`), and then a node `{token, prev, next, u8 = 0}` (0x10 bytes) is appended to the
list at `cb+0x120` (head), `+0x124` (tail) and `+0x128` (count). The node keeps the keyword's token id
(`po_annex` 0x8A1, `po_demand_state` 0x8A3, `po_transfer_provinces` 0x8A7, `po_remove_cores` 0xA38, ...;
full list in [raw/cb-keyword-map.txt](../raw/cb-keyword-map.txt)). `po_create_country` (0x8A6) and
`po_concede_defeat` (0x8A9) are not in `OnKeyword`'s switch and go to the unknown-key path, although
the peace code tests both. Every user walks the list and compares tokens: `CB_HasPeaceOption`
`0x0090A800` (EAX = cb, ECX = token, returns AL), or an inline loop.

### War goal **[I]**

`CWarGoal` (vtable `0x00E35DDC`):

| offset | field |
|---|---|
| `+0x0C` | `CCasusBelliType*` |
| `+0x10/+0x14` | country ref `{tag, index}` |
| `+0x18/+0x1C` | `actor`: the holder, who receives the provinces |
| `+0x20/+0x24` | `receiver` (save key): the target, who loses them |
| `+0x28` | province id: the target of state war goals (`po_demand_state`) |

How the AI and `CWarGoalDialog` fill `+0x28` was not traced. The AI scores targets per state, in the
`0x8A3` case of `FUN_00847b60`.

### Execution: `WarGoal_Execute` `0x0094E660` **[C]**

Runs one block per peace option of the war goal's CB, in a fixed order. `po_demand_state`
(`0x0094E707`):

```c
province = world->provinces[wg->province];                  // wg+0x28
state    = province->state;                                 // +0x188, the owner's CState
owner    = countries[state->ownerIndex];                    // +0x5C
if (provinces[owner->capital /* +0xBE4 */]->state == state && owner->stateCount /* +0xE4C */ == 1)
    Country_Annex(...);                                     // 0x00518620: the whole country
else
    TransferProvinces(&state->provinceIds /* +0x48 */, &wg->receiver /* +0x18 */,
                      &world->date /* +0xB0C */, 1, coresRef);   // call at 0x0094E844
```

`coresRef` is the owner's ref when the CB also has `po_remove_cores` (0xA38), else `---`.
`TransferProvinces` `0x004B5A90` (cdecl) walks a `vector<int>` of province ids and reads only its begin
and end. It also serves `po_transfer_provinces` (call at `0x0094E958`), events and effects.
`country+0xE44/+0xE48/+0xE4C` is the list of the country's states (head, tail, count; nodes are appended
in `FUN_00520ED0` when a state is created).

### Peace cost: `WarGoal_PeaceCost` `0x00951920` **[C]**

stdcall `(warGoal, int* out, bool)`, RET 0xC. 15 callers (peace dialog, AI). For `po_demand_state` it
sums a per-province value (`FUN_004B79A0`, then ×1000 scaling) over `state->provinceIds`. The loop at
`0x009519AD` re-reads `[state+0x48]` and `[state+0x4C]` on every pass. `po_transfer_provinces` does the
same over its states. The other options add a fixed define each.

### Infamy: `WarGoal_Infamy` `0x009500E0` **[C]**

Sums a define per peace option (`po_demand_state`: `defines+0x30 → +0x80`), multiplies by
`badboy_factor` (`cb+0xF4`) / 1000, and once more by a define when the relation flag `+0x34 → +0xFC` is
set. It does not depend on the size of the state.

### Other `0x8A3` users (not changed by `po_demand_province`)

| function | what |
|---|---|
| `FUN_0090AAF0` | walks the target's states (`+0xE44`) with `allowed_states`: which states the CB may target |
| `FUN_00952F10`, `FUN_00952FC0`, `FUN_009542C0` | war-goal UI and ticking war score; `0x8A3` and `0x8A7` share a path through `FUN_0094E4D0` (the states of the war goal) |
| `FUN_00847B60` | AI value of a war goal per state |

## Lexer tokens **[C]**

- `Tokens_RegisterKeywords` `0x00C381C0` fills a static array of 2744 entries `{int id; char name[0x100]}`
  (stride 0x104) from `0x01268FF8` to `0x013172D8`.
- On first use, `Lexer_Init` `0x00990100` (under `CLAUSEWITZ_LEXER_MUTEX`) builds the lookup table
  `0x00F0EC04/0x00F0EC08` from that array (`FUN_009901B0`). It sets `DAT_01258A64` = 1 and
  `DAT_01258A60` = highest id + 1.
- `Lexer_NextToken` `0x00990930` looks every word up when it is read: a known word gives its id, any
  other word 0xF, a number 0xC. The lookup is lazy, so a token registered before a file is parsed is
  recognised in that file.
- `Token_AddDynamic` `0x009902D0` is cdecl `(const std::string* name, bool allowExisting) -> id`.
  - If the name exists, it returns its id; with `allowExisting = 0` it first logs "Error on dynamic
    token: ..., Check if not already used elsewhere."
  - Otherwise the new id is the highest static id + the number of dynamic tokens (`0x0131C8DC` vector)
    + 1, and the name is inserted into the table.
  - The game uses it for names that become keywords, such as building names (`CBuilding_ctor`).
- `cb_types.txt`: the `CTokenReader` is built in `FUN_00639120`, then `CBManager_Load(CBManager_Get(),
  reader)` at `0x0063DCD2` parses it.

## `po_demand_province` (built, Script Extender `demand_province`)

The new keyword `po_demand_province = yes` in `cb_types.txt` lets a mod demand one province in a peace
deal.
- **Parsing:** at the `CBManager_Load` call, the word is registered with `Token_AddDynamic`.
  `CCasusBelliType`'s vtable slot 4 (`0x00E33734`) gets a wrapper: with `yes`, the wrapper adds the
  `po_demand_state` node (once, even when both keywords are present) and marks the CB.
- **Kept from `po_demand_state`:** AI, texts and infamy are those of `po_demand_state`. The dialog lists
  the target's provinces instead of its states (below), and `allowed_states` is checked per province.
  Infamy is tuned per CB with `badboy_factor`.
- **Changed, for a war goal of a marked CB:**

| site | vanilla | marked CB |
|---|---|---|
| `0x0094E844` (call `TransferProvinces`) | the state's province ids | a one-element list: `wg+0x28` |
| `0x0094E77B` (`CMP [owner+0xE4C], 1`, 7 bytes, becomes `CALL` + 2 `NOP`) | annex when the owner has one state (with the capital) | annex only when that state is also a single province |
| `0x009519AD` (7 bytes, `JMP` to a thunk) | cost over the state's provinces | cost of `wg+0x28` alone (a stand-in state whose `+0x48/+0x4C` cover that id) |
| `0x00945429` (`MOV EAX,[EAX+0xE44]` in `CWarGoalDialog_FillStateList`, 6 bytes, becomes `CALL` + `NOP`) | the target's states; a row stores its state's first province | one stand-in state per province of the target, so the dialog lists provinces |

**The war-goal dialog** ([diplomacy.md](diplomacy.md), `CWarGoalDialog`): the state list walks a list of stand-in
`CState`s, one per province of the target's states, made once per province and never freed (the rows keep
pointers to them). A stand-in has `CState`'s vtable with slot `0x20` (name) replaced by the province's
name (`CProvince` vt `0x1C`), and that province alone at `+0x48/+0x4C`; the rest is zero. Everything else
is vanilla and runs per province: the `allowed_states` scope (the first province), `WarGoal_IsPossible`,
the row (`CStateSelectionItem`), its tooltip, and the click (`CWarGoalDialog_SelectState` `0x00944B10`,
which stores `+0x48[0]` in the war goal). The substate list is not changed.

Not adapted: the ticking war score (`FUN_00952FC0`) and the AI's war-goal value still look at the
whole state. Patch sites: [patch_sites.csv](../symbols/patch_sites.csv) (`se_demand_province_*`).

## Open questions

1. Meaning of `country+0x37C/+0x37D`, `ai+0x19`, `country+0xE4C`, `g_aiWarStrengthRatio`.
   `country+0xE4C` answered on 2026-10-24: the number of the country's states (see "Peace options").
2. Order of the seven ×1000 factors at `+0xF4..+0x10C`.
3. Whether `CNullCasusBelliType` instances are in the manager vector.
4. Which province the war-goal dialog and the AI store in `CWarGoal+0x28` for a state war goal.
   **Answered for the dialog (2026-10-24):** `CWarGoalDialog_FillStateList` stores the state's **first
   province**, so `po_demand_province` from the dialog takes that one (see treaties.md). Since 2026-10-24
   the Script Extender lists provinces instead for a `po_demand_province` CB (see above). The AI's
   choice is still open.
5. Fields `+0x10` and `+0x20` of `CWarGoal`, and the third argument of `WarGoal_PeaceCost`.
