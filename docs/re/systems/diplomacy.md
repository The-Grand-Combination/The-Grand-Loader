# Diplomatic actions, peace offers and the diplomacy UI

This page covers how a diplomatic action is built, sent, answered and applied, and what the peace offer
and its dialogs do. It also lists the engine pieces a mod-made treaty can use; the treaty feature built on them (Script
Extender `treaty`) is in [treaties.md](treaties.md).

Sources: this repo's session of 2026-10-24 (decompilation and disassembly). Addresses, names and
confidence for every function are in [functions.csv](../symbols/functions.csv) (areas `diplomacy`,
`peace`, `command`, `ui`, `provinces`).

## Summary

- **The action type id is the script token of the action's name** (slot 8 returns it): `0x81E`
  peace_action, `0x470` warsubsidy, `0x931` make_cb_action, etc. **[C]**
- **One registry.** `DiplomaticAction_CreateByType` `0x0090E850` (cdecl `(int token)`) is a hard-coded
  switch. Unknown ids get a `CNullDiplomaticAction`. It serves the multiplayer command reader, the save
  loader (`CCountry` key `diplomatic_action` 0x8AF) and the diplomacy view. **[C]**
- **Flow.** The proposer queues a `CDiplomaticActionCommand` (type 900). Its Execute runs on every
  machine: a new action becomes pending on the recipient, an answered one is applied or declined. The AI
  answers on the host only, and its answer travels back as another command. **[C]**
- **Peace needs a war.** `CPeaceAction_Execute` and `AI_EvaluatePeaceOffer` return at once when
  `relation+0x34` (the `CWar*`) is null. **[C]**
- **War goals do not.** `CWarGoal` can be built on its own. `WarGoal_Execute`, `WarGoal_PeaceCost`,
  `WarGoal_Infamy`, `WarGoal_Prestige` and `WarGoal_IsValid(goal, 0, 0, 0)` only read the war for the
  Great War multipliers (null-checked) and for `po_install_communist`'s counter war goal. **[C]**
- **Every popup is a `CEU3Dialog`**, polled every frame through its vtable slot 1 and deleted when that
  returns false. **[C]**

## Classes

| class | vtable | size | ctor |
|---|---|---|---|
| `CDiplomaticAction` (abstract) | `0x00E3405C` (38 slots) | base 0x28 | inlined |
| `CNullDiplomaticAction` | `0x00E34154` | 0x44 | `0x00913230` |
| `CPeaceAction` | `0x00E34474` | 0x98, `CPeaceOffer` embedded at +0x28 | `0x00916A30` (factory), `0x00916900` (actor, recipient) |
| `CPeaceOffer` (data, not an action) | `0x00E340F8` | 0x70 | copy `0x00916C00` |
| `CDiplomaticActionCommand` | `0x00E34114` | 0x40, owned action at +0x3C | `0x0090F620` |
| `CWarGoalBaseAction` / `CAddWarGoalAction` / `CDeclareWarAction` | `0x00E341F4` / `0x00E34334` / `0x00E34294` | – / 0x68 / 0x6C | |
| `CWarSubsidiesAction` | `0x00E345B4` | 0x30, i64 amount +0x28 | inline |
| `CMakeCBAction` | `0x00E343D4` | 0x2C | inline |
| `CCrisisOfferAction` / `CCrisisBackDownAction` | `0x00E346F4` / `0x00E34514` | 0x68 / 0x9C | |

**[C]** for all rows.

### `CDiplomaticAction` base **[C]**

| offset | field |
|---|---|
| +0x08/+0x0C | actor `{tag, index}` (key `actor` 0x381) |
| +0x10/+0x14 | recipient (key `recipient` 0x382) |
| +0x18 | creation date |
| +0x1C | date sent (`last_command_date` 0x47A) |
| +0x24 | status: 0 new, 1 pending, 2 declined, 3 accepted |

### Virtual slots (the ones that matter) **[C]** unless tagged

| slot | offset | role | `CPeaceAction` |
|---|---|---|---|
| 2 / 4 | 0x08 / 0x10 | WriteFields / OnKeyword (save and network) | `0x00919660` / `0x00919690` (`terms` = the offer) |
| 6 | 0x18 | CreateCommand: +0x1C = now, `new CDiplomaticActionCommand(Clone())` | base `0x009115B0` |
| 8 | 0x20 | type id (token) | `0x81E` |
| 9 | 0x24 | name, base of the loc keys `<NAME>TITLE`, `<NAME>_DESC`, `<NAME>_OFFER`, `<NAME>_BUTTON` **[I]** | `PEACE` |
| 12 | 0x30 | Clone | `0x00919D00` |
| 13 | 0x34 | **show incoming** to a human recipient: `CDiplomacyDialog` ("DefaultDialog") | `0x00919D60` (then base `0x00913020`) |
| 14 | 0x38 | create dialog (`(owner, view)`) | `CPeaceDialog` |
| 18 / 19 | 0x48 / 0x4C | SetRecipient / SetActor | Peace refills the offer from the war |
| 20–23 | 0x50–0x5C | define indices: diplomatic cost, influence cost, relation on accept, on decline (0x15B = none) | 0xC4 / none / 0xC2 / 0xC3 |
| 26 | 0x68 | forbidden while at war with the recipient | 0 (subsidies: 1) |
| 27 | 0x6C | auto-accept (no consent: applied at once) | `0x00916E40` |
| 28 | 0x70 | subject to diplomat travel time | war with goals |
| 32 | 0x80 | **Execute** (on accept or auto) | `0x00916F00` |
| 34 | 0x88 | **OnDecline** | `0x00918860` |
| 36 | 0x90 | class-specific CanPerform, the last step of the gate | `0x009193A0` |
| 37 | 0x94 | localised "cannot send" reason | `0x00918C90` |

**Calling conventions (2026-10-24, confirmed by the Script Extender's `CTreatyAction`)** **[C]**: every slot
is thiscall. Slots 7, 9 and 16 take `(std::string* out)`, RET 4. Slot 37 takes `(out, int)`, RET 8. Slot 36
takes `(bool)`, RET 4. Slots 32 and 34 take `(ctx)`, RET 4, where `ctx` is the message-token context that
`Message_MakeContext` `0x005061E0` builds for the player country **[I]**. Base slots that need no war: 13
`0x00913020`, 18 / 19 `DiplomaticAction_SetRecipient` / `SetActor` `0x0092C720` / `0x0092C700`, 20–23
`DiplomaticAction_NoDefine` `0x00424B40` (0x15B), 37 `DiplomaticAction_EmptyReason` `0x00928300`.

`DiplomaticAction_CanPerform` `0x00911760` (ESI = action, `(bool)`, RET 4) is the generic gate. It
fails on any of: diplomat still travelling (slot 28, `relation+0x64`); actor == recipient; at war (slot
26); actor REB or without provinces; the vassal rules (slots 29–31); influence (slot 21) or diplomatic
points (slot 20) short. Otherwise it returns slot 36. **[C]**

### Action ids **[C]**

| id | class | id | class |
|---|---|---|---|
| 0x332 | alliance | 0x8C6..0x8C9 | ask / cancel ask / give / cancel give military access |
| 0x38D | declare war | 0x8CA | cancel alliance |
| 0x470 | war subsidies | 0x8CB | call ally |
| 0x81E | peace | 0x8D4 | ban embassy |
| 0x897 / 0x898 | add to / remove from sphere | 0x8D5 / 0x8D6 | increase / decrease relation |
| 0x8B0 / 0x8B1 | increase / decrease opinion | 0x8DD | add war goal |
| 0x8C1 | cancel war subsidies | 0x8E4 | gunboat |
| 0x8C2 | discredit | 0x931 | make CB (justify war) |
| 0x8C4 | expel advisors | 0x9FA / 0x9FB / 0xA02 | crisis offer / back side / back down |
| 0xA4F / 0xA50 | give / cancel unit command | 0 | `CNullDiplomaticAction` |

## Flow of an action **[C]**

1. **Build.** The diplomacy view (`CDiplomacyView_MakeAction` `0x0062F7B0`) reads `{token, alternative}`
   from `g_diplomacyActionTable` `0x00F095B8` for the clicked button and calls `CreateByType`, then
   SetActor (player) and SetRecipient (`view+0x388/+0x38C`). Declare war (0x38D) becomes peace (0x81E)
   when the two are at war. The click (`0x00624E60`) opens `action->vt[0x38](view+0x54, view)`.
2. **Send.** `cmd = action->vt[0x18]()`, then `country+0xCDC → vt[0x44]() → +0x38 → vt[0x18](cmd)`
   queues it. This is the same path as the building commands.
3. **Execute on every machine** (`CDiplomaticActionCommand_Execute` `0x0090F6B0`):
   - auto-accept (slot 27): Execute (slot 32) at once;
   - status 3: Execute, relation on accept, `Country_Recalculate` on both, messages;
   - new: status 1, slot 35, then `Country_AddPendingAction` (`0x005164C0`) appends a clone to
     `recipient+0xCE4`, but only on the machine that runs the recipient's AI or is the recipient. The
     costs are paid and the diplomat travel date is set;
   - status 2: OnDecline (slot 34), relation on decline, messages (keys in "Messages" below);
   - then a UI refresh, `world+0xB24 → vt[0xE0](6)`.
4. **Answer.**
   - AI: `AI_ProcessPendingProposals` `0x00820DF0`, inside `AI_CountryFrameJob`, on the host only. It
     turns the chance into `>= 50 ? 100 : 0`, compares it with CRT `rand() % 100`, sets the status to 3
     or 2 and sends `CreateCommand` again.
   - Human: each frame `GuiOwner_UpdateDialogs` shows every new pending action of the local player
     (`+0x20 == 0`) through slot 13: a `CDiplomacyDialog` on "DefaultDialog" (two shields, Title =
     slot 7, Description = slot 16, PROCEED / DECLINE), routed by `CInGameIdler_ShowDiplomaticDialog`
     (`0x00678580`). Its Accept / Decline (`0x00615E00` / `0x00615E40`) set the clone's status to 3 / 2
     and send it. The dtor removes the pending action.
5. **Save.** `CCountry_WriteFields` writes the pending list as `diplomatic_action` blocks; the loader
   rebuilds them through `CreateByType`. An id the game does not know loads as `CNullDiplomaticAction`.

## Messages **[C]**

- **Answer keys.** `CDiplomaticActionCommand_Execute` builds the key of an answer message from slot 10 (if
  non-empty), else the slot 9 name, plus `_THEY_ACCEPT` / `_WE_ACCEPT` / `_OTHER_ACCEPT` (strings
  `0x00E33864` / `0x00E33874` / `0x00E33880`, code from `0x009107C4`) or `_THEY_DECLINE` / `_WE_DECLINE` /
  `_OTHER_DECLINE` (`0x00E338A4`..). A new action needs those message types and their texts.
- **Message types are data.** `MessageTypes_Read` `0x006A4FA0` (stdcall `(manager, block)`, RET 8; deletes
  the block) reads `interface/messagetypes.txt` (call at `0x0063C4AA`; manager from `MessageManager_Get`
  `0x006A4F40`, singleton `g_messageManager` `0x01262EF4`). It needs `file_version >= 5` and makes one
  `CMessageType` (0x3C bytes, vtable `0x00E0BF74`) per entry, looked up by name (hash) and inserted if
  new. So a second file read through it adds types. The Script Extender reads
  `interface/se_messagetypes.txt` that way (copies of the `PEACE_*` settings).
- **Texts.** Each type reads `<TYPE>_1` .. `_6`, `_HEADER`, `_LOG`, `_BTN1..3`, `_MAP` and `_SETUP`.

### Localisation injection **[C]**

The game's CSV loader runs before a plugin could add texts, and `Localize` only serves the table. To add a
key at run time, do what `Localize` does for a missing key:
1. skip keys that exist (`StringHashMap_Find` `0x00A727A0` on `g_localisation+0x48`);
2. `Localisation_GetOrInsert` `0x009AD930` (ESI = key, EDI = `g_localisation`) returns the value;
3. `Localisation_AddVariant` `0x009AB900` (stdcall `(value, const std::string* text, selector*)`) with an
   all-zero 16-byte selector (what `Localisation_MakeSelector` `0x009A9070` builds for an empty string).

The Script Extender uses it for the treaty message texts and the treaty CB names.

## AI acceptance

`AI_EvaluateCommandChance` `0x0083DB20` and `AI_ProcessPendingProposals` switch on the type id.
Peace → `AI_EvaluatePeaceOffer`; subsidies → 100 if the amount > 5.0; sphere → `CanPerform`; the others
→ dedicated functions; **default → 0**, so the AI declines any id it does not know. A slot-27
(auto-accept) action from someone else always gets 100. **[C]**

### `AI_EvaluatePeaceOffer` `0x0083DE70` **[C]** for the structure and defines, **[I]** for the reading

EAX = `CPeaceAction*`, cdecl `(tag, idx, bool asActor)`, returns 0 or 100. `self` = the recipient (or the
actor when `asActor`). All defines are in the AI block (`g_economyDefines+0x60`).

```c
if (!CanPerform(0) || !(war = rel(self, other)->war)) return 0;
wsSelf = War_GetWarScore(war, self, other); wsOther = War_GetWarScore(war, other, self);  // -100..100
cost = PeaceOffer_GetTotalCost(offer);      // (sum of selected WarGoal_PeaceCost + 500) / 1000
// count our own goals (nOurs) and how many the offer gives us (nGiven); annexMe; great-war goals
force  = (0.75 * min(land ratio, 3) + 0.25 * clamp(naval ratio, 0.2, 2) - 1) * 100;
months = PEACE_TIME_MONTHS - months since the war started;
W = PEACE_WAR_EXHAUSTION_FACTOR * self.warExhaustion
  - PEACE_FORCE_BALANCE_FACTOR * force
  - PEACE_WAR_DIRECTION_FACTOR * direction (x PEACE_WAR_DIRECTION_WINNING_MULT when winning)
  - months (x PEACE_TIME_FACTOR, x PEACE_TIME_FACTOR_NO_GOALS, once the war is old)
  - PEACE_BASE_RELUCTANCE;                  // allies: each term x its PEACE_ALLY_*_MULT
if (annexMe) W -= 100;  // great-war goals: -100 / +50
if (nGiven > 0)  return (nGiven == nOurs && leader) || (W > 0 && min(wsSelf - W/2, 101) <= cost) ? 100 : 0;
if (cost > 0 && wsOther < 0) return 0;      // a demand from a side that is losing
...                                         // white peace cases
return cost <= min(W/2 + wsOther, 101) ? 100 : 0;
```

**The demand rule:** a demand is accepted when `cost ≤ wsOther + W/2`, capped at 101. The cost is in
war-score points. The force-balance term adds 0.5 × the strength of the countries each side is at war
with (`country+0xDA8`); it seems to count the current enemies twice **[I]**.

- **Decline penalty** (`CPeaceAction_OnDecline` `0x00918860`): when the actor's war score is above 50 and
  the offer is AI-acceptable from the actor's side, or the offer is marked `better`, the recipient gets
  GOOD_PEACE_REFUSAL_WAREXH war exhaustion and GOOD_PEACE_REFUSAL_MILITANCY on every pop. **[C]**
- **AI proposals** (`AI_ManagePeace` `0x0083C7D0`): for each enemy, build a peace action with every goal,
  drop goals from the last to the first until both sides evaluate 100, then send it. Human targets get a
  cooldown (`ai+0x84`). **[I]**

## Peace offer and peace execution

### `CPeaceOffer` (at `CPeaceAction+0x28`) **[C]**

| offset | field |
|---|---|
| +0x08 | u8 demand: 1 = the actor demands, as the winner |
| +0x09 | u8 better (`PO_ISBETTER`; forces the decline penalty) |
| +0x0C / +0x1C | vector<int> selection flags for the goal vectors +0x2C / +0x3C (2 = locked) |
| +0x2C / +0x3C | vector<CWarGoal> (stride 0x40) of each side |
| +0x4C / +0x5C | vector<CountryRef> the two sides **[I]** |
| +0x6C | u8 valid, set by `PeaceOffer_FillFromWar` |

### `CWarGoal` (0x40 bytes, vtable `0x00E35DDC`) **[C]**

`CWarGoal_Ctor` `0x0094BDB0`: EAX = this, ECX = cb, stack `(actorTag, actorIdx, receiverTag,
receiverIdx)`, RET 0x10. No heap members; held by value.

| offset | field (save key) |
|---|---|
| +0x0C | `CCasusBelliType*` (`casus_belli`) |
| +0x10/+0x14 | `country`: third party (transfer destination, released country, sphere member) |
| +0x18/+0x1C | `actor`: the holder and beneficiary (gets provinces, annexes) |
| +0x20/+0x24 | `receiver`: the target, the side that loses |
| +0x28 | `state_province_id` |
| +0x2C / +0x30 / +0x34 / +0x38 / +0x3C | score, change, date, is_fulfilled, region (po_colony) |

### `CPeaceAction_Execute` `0x00916F00` **[C]**

1. Returns without a war. The winner is the actor if `demand`, else the recipient.
2. Truce months = max(BASE_TRUCE_MONTHS, each goal's CB `truce_months` `+0x110`).
3. `PeaceOffer_SortGoals` (peace_order **[I]**), then per goal (po_annex goals in a second pass): when
   selected and `WarGoal_IsValid(goal, 0, 0, 0)`, `WarGoal_Execute`; otherwise `WarGoal_Fail`
   (`0x00951ED0`, the failure penalties of the holder).
4. Per winner × loser: `Country_ReturnOccupiedProvinces` both ways, and `relation+0x6C` = now + truce
   months, both directions.
5. Leaves the war, and archives it if it ended.
6. Per participant: `Military_RepositionArmies`, port blockades, unit flags, `Economy_CalculateDistribution`
   when territory changed, `AI_DailyUpdate`, messages. Losers get the party loyalty hit.
7. **No infamy at peace.** Infamy is added when a goal is declared or added. **Prestige** is applied
   inside `WarGoal_Execute`.

### Peace options outside a war **[C]**

| option | `WarGoal_Execute` does | war needed |
|---|---|---|
| `po_annex` 0x8A1 | `Country_Annex(actor, cores?, receiver)` | no |
| `po_demand_state` 0x8A3 | transfers the state of +0x28; annexes if it is the receiver's last state and holds the capital | no |
| `po_transfer_provinces` 0x8A7 | needs +0x14. Releases a dead third country, or transfers the states to it, plus relation | no |
| `po_add_to_sphere` 0x8A2, `po_clear_union_sphere` 0x264 | sphere and influence changes | no |
| `po_release_puppet` 0x8AA, `po_make_puppet` 0x942 | `Vassal_Release` / new `CVassal` | no |
| `po_reparations` 0x8A5 | `CPayingReparations`: each day a define share of the payer's income, for a define number of years | no |
| `po_disarmament` 0x8A4 | destroys units with the synced game RNG; `CDisarmament` | no |
| `po_remove_prestige` 0x8A8 | prestige loss | no |
| `po_gunboat` 0x6C2 | `CGunboating` agreement | no |
| `po_destroy_forts` / `_naval_bases` 0xA2F / 0xA30 | building levels to 0 in the target states | no |
| `po_install` / `uninstall_communist` 0x975 / 0x976 | government and sphere | install reads the war, null-checked |
| `po_colony` 0xA4C | uses the region at +0x3C | no **[I]** |
| `po_status_quo`, `po_concede_defeat` | cost and prestige only | – |
| every goal | the CB's `on_po_accepted` (`+0x1AC`; scope = actor, THIS = receiver), then prestige +P to the actor, −P to the receiver | no |

`WarGoal_IsValid(goal, 0, 0, 0)` checks only: the CB is real, the receiver owns provinces,
`allowed_countries`, and `allowed_states` over `WarGoal_GetTargetStates` with each state owned by the
receiver. It does not run `can_use` or `is_valid`, and with war and crisis 0 it returns true after these
checks (`0x00952D1B`). **[C]** It keeps the third country (FROM) only in the THIS scope, so nested
conditions on FROM fail (casus-belli.md, "Scopes"). **[C]**

Options with a third country (`+0x10/+0x14`), from `WarGoal_Execute` **[C]**:

| option | without the third | with it |
|---|---|---|
| `po_add_to_sphere` | the receiver joins the actor's sphere | the third leaves the receiver's sphere (influence reset, `MAX_INFLUENCE` define `+0x404`) and joins the actor's (`Sphere_Remove` / `Sphere_Add`) |
| `po_release_puppet` | nothing | `Vassal_Release(&third, &receiver)` |
| `po_make_puppet` | the receiver's vassals are released, then a new `CVassal` actor / receiver | – |
| `po_install_communist_gov_type` | the receiver takes the actor's government type (`Country_SetGovernment` `0x00513A90` **[I]**) and joins the actor's sphere if the actor is a great power; the counter war goal for war allies only with a war | – |

## Defines

Root `g_economyDefines` `0x012586DC`. The blocks are +0x30 diplomacy, +0x40 country and +0x60 AI (loader
`0x0042E410`). **[C]**

| block + offset | define | used by |
|---|---|---|
| +0x30 +0x00C..+0x044 | PEACE_COST_* per option | `WarGoal_PeaceCost` |
| +0x30 +0x048..+0x088 | INFAMY_* (`+0x080` DEMAND_STATE) | `WarGoal_Infamy` |
| +0x30 +0x08C..+0x100 | PRESTIGE_*_BASE, PRESTIGE_* | `WarGoal_Prestige` |
| +0x30 +0x2DC..+0x2E4 | WAR_FAILED_GOAL_MILITANCY / _PRESTIGE_BASE / _PRESTIGE | `WarGoal_Fail` |
| +0x30 +0x300..+0x384 | *_RELATION_ON_ACCEPT / _DECLINE, *_DIPLOMATIC_COST, *_INFLUENCE_COST per action | slots 20–23 |
| +0x30 +0x400 | BASE_TRUCE_MONTHS | peace |
| +0x30 +0x444 / +0x448 | GOOD_PEACE_REFUSAL_MILITANCY / _WAREXH | `CPeaceAction_OnDecline` |
| +0x40 +0x068 | DIPLOMAT_TRAVEL_TIME | `Relation_SetDiplomatTravel` |
| +0x60 +0x050..+0x080 | PEACE_BASE_RELUCTANCE, PEACE_TIME_MONTHS, PEACE_TIME_FACTOR, PEACE_TIME_FACTOR_NO_GOALS, PEACE_WAR_EXHAUSTION_FACTOR, PEACE_WAR_DIRECTION_FACTOR, PEACE_WAR_DIRECTION_WINNING_MULT, PEACE_FORCE_BALANCE_FACTOR, PEACE_ALLY_BASE_RELUCTANCE_MULT, PEACE_ALLY_TIME_MULT, PEACE_ALLY_WAR_EXHAUSTION_MULT, PEACE_ALLY_WAR_DIRECTION_MULT, PEACE_ALLY_FORCE_BALANCE_MULT (4 bytes apart) | `AI_EvaluatePeaceOffer` |

`CCasusBelliType` factors (from `OnKeyword`'s jump table) **[C]**: `badboy_factor` +0xF4, `prestige_factor`
+0xF8, `peace_cost_factor` +0xFC, `penalty_factor` +0x100, `break_truce_*` +0x104..+0x10C, `truce_months`
+0x110, `good_relation_*` +0x114..+0x11C, `great_war_obligatory` +0x1D4, `all_allowed_states` +0x1DC.

## Commands and multiplayer

`CCommand` (base vtable `0x00DEDA5C`, 16 slots) **[C]**:

| offset | field | slot | role |
|---|---|---|---|
| +0x24 | player id | 2 / 4 | serialize / read one field (value text at `parser+0x22C`) |
| +0x2C / +0x30 | type id (+0x30 = hash key) | 6 | Execute |
| +0x34 | u16 tick stamp | 7 | type id |
| +0x36 | u8 immediate | 12 | keep for joiners |
| +0x38 | identity | 13 | Create (clone, `std::string` by value, RET 0x1C) |
| +0x3C | subclass fields | 14 | Validate (Execute runs only if true) |

- **Factory.** `Commands_RegisterPrototypes` `0x0081B290` (from WinMain) puts one prototype per class
  into `g_commandProtoTable` `0x0131C960` (0x1FF buckets, node `{proto, next}`, key `+0x30 % 0x1FF`). On
  receive, `CommandFactory_Create` `0x009A2740` clones the prototype (slot 13) and reads the fields. An
  unknown type logs "Couldn't find command" and is dropped. **[C]**
- **The wire format is text:** token name `=` value (`CmdStream_WriteToken` `0x009A0940`). Field names are
  looked up by name, so dynamic tokens work when every peer registers them. **[C]**/**[I]**
- **`CConquerProvinceCommand`** (vtable `0x00DFC6E4`, 0x48 bytes, type 0x298): `+0x3C` province, `+0x40`
  receiver. Execute = `TransferProvinces({province}, receiver, today, 1, ---)`; Validate only checks the
  receiver. It is registered, but vanilla never sends one. **[C]**

## Province transfer helpers

`TransferProvinces` `0x004B5A90` does, per province **[C]**:
1. wipes the pops' money (`pop+0x250`);
2. calls `Province_SetController`, `Province_SetOwner` and optionally `Province_RemoveCore`;
3. notifies the owner listeners and resets the nationalism timer;
4. removes a now-empty old owner from its sphere and recomputes the unit flags;
5. while the game runs (`world+0xD1C`), deletes constructions and timed modifiers;
6. finally calls `Country_RebuildStates(receiver)`, which joins or creates states, moves factories,
   fires `on_state_conquest` and deletes emptied states.

**What it does not do:**
- **Death of the old owner** when it loses its last province. `Country_Annex` does that cleanup.
- **The capital.** `Country_ValidateCapital` `0x0050E540` fixes it daily, or when called.
- **Expelling units.** The peace path does that afterwards with `Military_RepositionArmies`.
- **The old owner's units.** See below.

### Units of a dead tag **[C]**

A country's armies and fleets share one list, `country+0x7B4` (node `{unit, prev, next}`, count
`+0x7BC`); `unit+0xC4` is the owner's index. `World_DeleteArmy` `0x0068AA10` (thiscall, `ECX` = world,
the unit on the stack, `RET 4`) is the full deletion: it unloads a fleet's armies, takes the unit out of
its battles, returns the regiments' pops and removes it from the owner's list.

Of the ten callers of `TransferProvinces`, only `Country_Annex` deletes the victim's units: after the
transfer, `while (victim+0x7BC != 0) World_DeleteArmy(world, first unit)` (`0x00518E5F`..`0x00518E9A`).
`SecedeProvinceEffect_Execute` walks the old owner's list but deletes only armies without regiments
(`unit+0x40 == 0`); the others (release, peace transfers, the conquer-province command) touch no unit.
A tag that loses its last province by one of those keeps every army and fleet where it stands, often in
other countries' land: ghost units. `Country_DailyUpdate` still walks them, but `AI_DailyUpdate` is
skipped for a tag without provinces, so nothing moves them. Rebels are the exception that must stay:
`REB` never owns a province and owns every rebel army.

`fix_ghost_units` deletes them with the annexation's loop, before each day's `DayTick` (patch site
`cp_ghost_units_day`).

Other helpers **[C]**:

| helper | address | effect |
|---|---|---|
| `Country_AddPrestige` | `0x005341A0` | ESI = country, `(int ×1000)` |
| `Country_ChangeRelation` | `0x005179E0` | EDI = A, EDX = &B, `(delta)`; both directions |
| `Date_AddMonths` | `0x00424810` | adds months to a date |
| truce | `relation+0x6C` | end date, written both ways |
| infamy | `country+0x1430` (×1000) | no helper; the effect adds to it directly |
| treasury | `CCountry+0xE78` | i64, 2^15 fixed point. Effects add `v × 32768 / 1000` with no ledger. `Country_SpendMoney` `0x00525770` books an expense and borrows when short |

## Diplomacy UI

### Dialogs **[C]**

- `CEU3Dialog_ctor` `0x00640680`: ECX = gui owner, `(this, const std::string* eu3dialogtype)`, RET 8.
  - It instantiates the named type and wires `AgreeButton` → slot 6 and `DeclineButton` → slot 7.
  - Base size 0x68, vtable `0x00E08694` (18 slots).
- To show a dialog: `dlg->vt[0x28]()`, then `GuiOwner_AddDialog` `0x006480E0` (ESI = owner, `(dlg)`).
  - Every frame, `GuiOwner_UpdateDialogs` `0x006481D0` calls slot 1.
  - When slot 1 returns false, the dialog is deleted (`vt[0](1)`). Closing = `+0x64 = 1`.
- Slots:

| slot | role |
|---|---|
| 1 | per-frame refresh, returns "keep open" |
| 4 | tooltip `(std::string* out, element*)` |
| 6 | agree |
| 7 | decline |
| 15 / 16 | title / description |

- Helpers: shields (country flags) `0x00641630` / `0x006416A0`; title / description `0x00641710` /
  `0x006417B0`.

| dialog | gui type (file) | made by |
|---|---|---|
| `CPeaceDialog` (0xC8) | `SetupPeaceDialog` (wardeclaration.gui) | `CPeaceAction` slot 14 |
| `CWarGoalDialog` (0xAC) | `DeclareWarDialog` (wardeclaration.gui) | slot 14 of declare war, add war goal, crisis offer |
| `CStandardDiplomacyDialogue` (0x78) | `DefaultDiplomacyDialog` | slot 14 of simple actions (the sender's box, with the AI chance) |
| `CDiplomacyDialog` (0x70) | `DefaultDialog` (eu3dialog.gui) | slot 13: **the box every human recipient sees**, peace included |
| `CCBCreationDialog` (0x78) | `MakeCBDialog` | `CMakeCBAction` slot 14 (`0x00926D00`): justify war |

### `CPeaceDialog` **[C]**

- `+0x6C` is a clone of the action. The goals are listed in `setup_goals.wargoal_list`, one
  `CPeaceGoalEntry` (`peace_goal_item`) per goal, each with:
  - a `select` checkbox;
  - `country_flag` / `country_flag2`;
  - `select_label`;
  - `score` = `WarGoal_PeaceCost`.
- `pick_side` switches between offer and demand. The `warscore` text comes from `War_GetWarScore`.
- Slot 1 (`CPeaceDialog_Update` `0x00947210`) runs every frame:
  - AgreeButton is enabled from `CanPerform`;
  - score = `PeaceOffer_GetTotalCost`;
  - for an AI recipient, `PEACE_WILL_ACCEPT` / `PEACE_WILL_NOT_ACCEPT` from `AI_EvaluatePeaceOffer`.
- OnAgree (`0x00948CF0`) queues `action->vt[0x18]()`.
- **It is the sender's screen only.** The recipient gets the DefaultDialog box with the terms as text
  (`CPeaceAction_GetOfferText` → `PeaceOffer_GetTermsText`, one bullet per selected goal). **[C]**
- `CPeaceDialog_Setup`, OnOffer / OnDemand, slot 1 and the recipient text read the war with no null
  check, so neither class can be used without a war. **[C]** reads, **[I]** crash.

### `CWarGoalDialog` **[C]**

- **CB list** (`CWarGoalDialog_FillCbList` `0x00940090`). A CB is listed when all three hold:
  1. the actor holds it on the target, or it is `always`, or the two are at war and it is
     `constructing_cb` (crisis: the crisis flag);
  2. `can_use` passes (vt 0x20);
  3. `WarGoal_IsPossible` passes.
- **Rows.** Each is a `CGoalEntry` (`wargoal_item`): the name via `CCasusBelliType_GetDisplayName`, the
  icon frame from `cb+0x0C` (`sprite_index`; `months` is `+0x134`). **[C]**
- **Scopes.** `can_use` runs on the target scope with THIS = the actor (`0x00940205..0x00940256`). The
  country list (`CWarGoalDialog_FillCountryList` `0x009456C0`) evaluates `allowed_countries` the same way,
  with the candidate as FROM in the THIS scope (`+0x2C/+0x30`). **[C]**
- **Click** (`CWarGoalDialog_SelectCb`): opens the country list (`allowed_countries`), the state list
  (`allowed_states`, unless `all_allowed_states`) or the substate list.
- **State list** (`0x009452A0`): the target's states whose first province passes `allowed_states` and
  `WarGoal_IsPossible`. **There is no map pick.**

### Diplomacy view **[C]**

- `CDiplomacyView` (vtable `0x00E07450`, 0x398 bytes): selected country at `+0x388/+0x38C`, gui owner at
  `+0x54`.
- `CDiplomacyView_Setup` `0x00622270` (stdcall `(view)`, RET 4; called by the ctor at `0x0061BE1B` and by
  `FUN_0061BF70` at `0x0061C096`) creates 17 `diplomacy_option` windows (`g_gui->vt[0x2C](type, (world+0xB24)->vt[0x3C]())`),
  attaches them to the view window (`+0x4C`) as `action<i>` (`vt[0xBC](name, child)`), binds every
  `action_option` button to `CDiplomacyView_OnActionClick` (glue at `+0x178`) and stores the index in the
  button (`vt[0xD4]`; read back by `vt[0x80]` on click). The grid: the positionTypes `diplomacy_actions_pos`
  and `diplomacy_actions_wrap_count` (`g_gui->vt[0x48](name)`, `+0x64` x, `+0x68` y) give the origin and the
  number of columns; the buttons fill column by column (17 / columns rows, the first 17 % columns columns one
  row longer), each at origin + window size (`vt[0x60]`) times the cell, set by `vt[0x18](packed {i16 x, i16 y})`,
  then `(window+0x494)->vt[0xB0]()`; Setup ends with `(view window+0x18)->vt[0x34]()` (`0x00624266`, shows the window and its children; a
  window added later needs it too or it is not drawn) and the view's slot 4 (`0x007539D0`: window `vt[0x24]`,
  `(+0x50)+0x18` `vt[0x38]`), which hides it again. Slot 7 (`0x00753B60`) returns `window+0x67`, shown. The Script Extender adds an 18th cell this way (treaties.md).
- The button text is `<NAME>_BUTTON`: "Justify war" is `MAKE_CB_BUTTON` (vanilla `housedivided.csv`). **[C]**
  `CDiplomacyView_MakeAction` keeps 0x931 (its alternative is also 0x931). A mod may rename the text (e.g. "Diplomatic
  Play") in its own localisation.
- `CDiplomacyView_RefreshActions` re-enables the buttons from `CanPerform` (call at `0x0062B9FC`, ESI =
  action, `[EBP+8]` = view; the Script Extender's `se_treaty_button` site).

### Widgets **[C]**

- Window finders, each `thiscall(const std::string*)`, returning 0 when absent:

| slot | finds |
|---|---|
| `0x34` | button |
| `0x3C` | text box |
| `0x40` | edit box |
| `0x44` | icon (icons only) |
| `0x48` | checkbox |
| `0x4C` | scrollbar |
| `0x6C` | subwindow |
| `0x7C` | listbox |

  `0xBC` adds a subwindow.
- Buttons:

| slot | does |
|---|---|
| `0x34` / `0x38` | show / hide |
| `0x48` | SetFrame (`CPeaceDialog_Update` sets a tab to frame 2 when active, 1 when not) |
| `0xB8` / `0xBC` | enable / disable |
| `0xCC` | SetText |
| `0xD4` | SetUserIndex |
| `0xE0` | SetSprite |
| `+0x54` | observer list (`vt[4]` add) |

- Icons share `0x34` / `0x38` / `0x48` with buttons (`Icon_vftable` `0x00E3A930`: `Icon_Show` `0x009C43C0`,
  `Icon_Hide` `0x009C43F0`, `Icon_SetFrame` `0x009C4410`).
- Text boxes: assign `tb+0xDC` (`0x00408ED0`, thiscall `(const std::string*, pos, count)`), then
  `TextSprite_SetText` `0x00A5ECC0` (ESI = `tb+0xA0`).
- Edit boxes (`editBoxType`, window finder `0x40`, `EditBox_vftable` `0x00E39878`): show / hide at `0x34` /
  `0x38` as buttons. The text widget at `+0x54` (`EditText_vftable` `0x00E39938`) holds the text as a
  std::string at its `+0x18`: `vt[0x40]` sets it (`EditText_SetText` `0x009F5290`), `vt[0x4C]` copies it
  out (`EditText_GetText` `0x009F5950`). Typing reaches the focused one through `WM_CHAR` (ui-console.md).
- Listbox: `vt[0x5C]` clear, `vt[0x10](item, 1)` add.
- Button glue (0x2C bytes, vtable `0x00E086E0` and its folded twins): `+4` owner, `+0x08` click `()`,
  `+0x20` click `(button)`, `+0x24` right click. The slots null-check their handler, so a glue with only `+0x20`
  set is safe. **[C]**
- Slider: the template is `transfer_slider` (invest_project.gui). Range via `ScrollbarRange_Make`,
  `(sb+0x54)->vt[0x1C](value, 1, 1)`, and `vt[0x10]()` returns the float value.
- Flags: `Country_GetFlagTexture` `0x0052FC10` → sprite `vt[0x150]`.

### Loading `.gui` / `.gfx` **[C]**

- `EU3App_LoadResources` `0x00635207` lists `interface/*.gfx`, then `*.gui`, of the mod and of the game
  (a mod file replaces the game file of the same name).
- It parses each file: `FileReader_ctor`, `ParserBlock_ctor`, `(target+4)->vt[0xC](block)` into
  `app+0x11C` (gfx) or `app+0x120` (`g_gui` `0x01258A80`).
- It ends with `Gui_Finalize` (call at `0x0063690D`).
- The game folder is mounted at the PhysFS root (`0x0097A38E`). So a path such as
  `mod/script_extender/interface/x.gui`, or a `texturefile` under `mod/script_extender/gfx/`, resolves
  **[I, high]**.
- The exe exports PhysFS 2.0 (`PHYSFS_enumerateFiles`, `PHYSFS_exists`, ...).
- **Confirmed by the Script Extender (2026-10-24)** **[C]**:
  - the per-file sequence: `FileReader_ctor` `0x0098FFC0`, the reader's vtable set to `CTokenReader`
    `0x00E37C3C`, `ParserBlock_ctor` `0x009A0BF0` (EDX = name; `(mem, reader, 1)`, RET 0xC), then
    `(target+4)->vt[0xC](block)` and delete the block;
  - **a `.gfx` must be parsed before the end of the game's `.gfx` loop** (`0x00635D89`). Parsed later
    (for example at `Gui_Finalize`), its sprite's texture at `+0xD0` stays null and drawing it crashes
    (`Sprite_Draw` `0x00A28990` at `0x00A28A6B`). A `.gui` parsed before `Gui_Finalize` works;
  - a gui type is looked up with `owner->vt[0x34]()` (the GUI) `+0x34` → `vt[4](const std::string*)`,
    0 when unknown. `CEU3Dialog_ctor` would crash on an unknown type, so check first;
  - `TextureManager_LoadFile` replaces `\` with `/` (`String_ReplaceAll` `0x00989720`) and tries `.dds`
    before `.tga`. `texturefile` paths are relative to the game folder.

## Open questions

1. The save key of the status (+0x24), and whether a status-1 action is re-processed after loading.
2. RTTI or `dynamic_cast` on diplomatic actions (it matters for a DLL vtable without RTTI). The treaty
   vtable carries `CDiplomaticAction`'s locator; no problem seen so far.
3. `CCountry+0xDA8` read as "at war with" against the older "vassals"; `Relation +0x20/+0x28/+0x30`.
4. Whether `TransferProvinces` / `Country_Annex` in peacetime leave units or wars in a bad state.
5. Whether calling `Gui_Finalize` again, or parsing a `.gfx` block late, is safe. **Answered for `.gfx`
   (2026-10-24):** not safe after the `.gfx` loop (see "Loading"). A `.gui` before `Gui_Finalize` is fine.
6. The meaning of `cb+0x0C`. **Answered (2026-10-24):** `sprite_index`, the icon frame; `months` is `+0x134`.
7. Whether `CConquerProvinceCommand` round-trips in multiplayer (never exercised by vanilla).
