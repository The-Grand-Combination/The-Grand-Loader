# Treaties (planned Script Extender feature)

**Status: research only (2026-10-24). Nothing is built.**

The feature lets two countries at peace trade terms: a province (or a state) for a province, plus money.
The other side accepts or refuses, as with a peace offer. This page maps every piece of the plan to the
engine facts in [diplomacy.md](diplomacy.md) and [casus-belli.md](casus-belli.md), and lists the hooks
and the risks. The addresses are in [functions.csv](../symbols/functions.csv).

## Design (from the request)

1. **Terms are CBs.** A CB in `cb_types.txt` with `is_treaty_cb = yes` can be used as a treaty term. A
   term is that CB's peace options applied in peacetime, for example `po_demand_state` or
   `po_demand_province`.
   - Treaty CBs are ordinary CB entries, so a mod tunes each one with its usual fields: `peace_cost_factor`,
     `badboy_factor`, `prestige_factor`, `truce_months`, `allowed_states`, `can_use`.
   - A term can go either way: one I demand, or one I give.
2. **Money** is an extra term: a lump sum either way.
3. **Entry point.** The diplomacy view's "Justify war" button becomes **"Diplomatic Play"**. A click
   offers two choices:
   - **Justify Casus Belli**, the vanilla dialog;
   - **Initiate Treaty**, the new flow.
4. **Same screens as the game.** CB list and state list as when adding a war goal. The recipient gets the
   same box as an incoming peace offer. The window's `.gui` / `.gfx` live in `mod/script_extender/`.
5. **AI.** It evaluates a treaty like a peace offer, with a heavier weight on what it gives away.

## Engine facts the plan relies on

| need | fact | conf |
|---|---|---|
| a term without a war | `CWarGoal` builds standalone (`CWarGoal_Ctor` `0x0094BDB0`). `WarGoal_Execute`, `WarGoal_PeaceCost`, `WarGoal_Infamy`, `WarGoal_Prestige` and `WarGoal_IsValid(goal, 0, 0, 0)` read the war only for null-checked Great War multipliers | C |
| peace options safe in peacetime | every option except `po_install_communist` (whose war read is null-checked) and `po_colony` (not checked); see the table in diplomacy.md | C / I |
| reuse vanilla peace | impossible: `CPeaceAction`, `CPeaceDialog` and `AI_EvaluatePeaceOffer` need `relation+0x34` (several reads are not null-checked) | C |
| sync, saves and the incoming box | the generic diplomatic-action flow: `CDiplomaticActionCommand`, the pending list `CCountry+0xCE4` (saved), the recipient box `CDiplomacyDialog` ("DefaultDialog"), and the host-only AI answer | C |
| a new action type | the only registry is `DiplomaticAction_CreateByType` `0x0090E850` (switch). A hook at its entry covers MP, saves and the UI | C |
| AI for an unknown type | `default` → 0 in `AI_ProcessPendingProposals` `0x00820DF0` and `AI_EvaluateCommandChance` `0x0083DB20` | C |
| the button | `CMakeCBAction` (token 0x931) slot 14 = `0x00926D00`, vtable entry `0x00E3440C`; text key `MAKE_CB_BUTTON` | C |
| CB and state lists | `CWarGoalDialog` (`DeclareWarDialog`): `FillCbList` `0x00940090` (one call site `0x0093FD58`), `FillStateList` `0x009452A0` (call sites `0x00944A50`, `0x00944D00`), OnAgree slot 6 `0x0093FF70` | C |
| province from the state list | the state list stores the state's **first province** in the goal | C |

## Recommended architecture

### 1. Keyword `is_treaty_cb`

The same technique as `po_demand_province`:
1. Register the word with `Token_AddDynamic` at the `CBManager_Load` call.
2. In the `CCasusBelliType` OnKeyword wrapper, mark the CB for `yes`.
3. Keep a vector of the marked CBs.

The wrapper and the load thunk already exist in `demand_province.cpp` and can be shared.

### 2. A new diplomatic action, `CTreatyAction`

A DLL-made subclass of `CDiplomaticAction`. Its type id is a dynamic token, for example `treaty_action`.

- **Layout.** The same as `CPeaceAction` up to 0x98, so the offer helpers work on it. Add two i64 money
  fields at +0x98 / +0xA0 (2^15 fixed point).
  - `+0x28` is an embedded `CPeaceOffer`.
  - `offer+0x3C` (`war_goal`) holds the terms the actor demands.
  - `offer+0x2C` (`war_goal2`) holds the terms the actor gives.
  - All flags are 1 (selected).
- **Why that layout:**
  - `CPeaceOffer_WriteFields` / `OnKeyword` already save and load the goals.
  - `PeaceOffer_GetTermsText` already writes the recipient's bullet list **[I: it is not one of the
    war-reading paths]**.
  - `PeaceOffer_GetTotalCost` already sums the costs.
- **Registry.** Hook the entry of `DiplomaticAction_CreateByType`: for our token, return a new
  `CTreatyAction`; otherwise call the original. This covers the multiplayer command reader, the save
  loader (key 0x8AF) and the UI.
- **Vtable (38 slots, built in the DLL)** **[I]**:
  - Reuse from base: 1, 3, 5, 6, 7, 10, 11, 15, 31, 33, 35. Reuse the base slot 13 `0x00913020`: it
    gives the recipient the DefaultDialog box.
  - Implement:

| slot | does |
|---|---|
| 0 | dtor: `CPeaceOffer_DestroyFields` + free |
| 2 / 4 | WriteFields / OnKeyword: base `0x009112B0` / `0x00911470`, plus `CPeaceOffer_WriteFields` / `OnKeyword` under a `terms` key, plus the two money values |
| 8 | the token |
| 9 | `"SE_TREATY"`, so the loc keys are `SE_TREATYTITLE`, `SE_TREATY_DESC`, `SE_TREATY_BUTTON` |
| 12 | Clone |
| 14 | the editor dialog (below) |
| 16 | the recipient text: `SE_TREATY_OFFER` + `PeaceOffer_GetTermsText` + money lines |
| 18 / 19 | SetRecipient / SetActor |
| 20–23 | define indices: no diplomatic or influence cost, or reuse the alliance ones |
| 24–30 | rule flags; **26 = 1**: not while at war with the recipient |
| 27 | auto-accept = 0 |
| 32 | Execute (section 5) |
| 34 | OnDecline |
| 36 | CanPerform (section 4) |
| 37 | the "cannot send" reason |

- **AI answer.** Patch the `default` of the type switch in `AI_ProcessPendingProposals` (and in
  `AI_EvaluateCommandChance` for the tooltip). For our token, return our evaluator's 0/100. The decision
  is made on the host and travels as a command, as for every action.
- **What this gets for free:** multiplayer sync, the pending list and its save, the recipient box with
  shields, title, text and PROCEED / DECLINE, the diplomat travel time, the relation change on
  accept / decline and the messages.

The UI agent also proposed an alternative: our own `CEU3Dialog` on "DefaultDialog" plus our own command,
with no action subclass. It needs a new command type in the prototype table (`g_commandProtoTable`), our
own pending state (lost on save unless stored elsewhere) and our own recipient routing. More work, less
vanilla behaviour. **Not recommended.**

### 3. Screens

1. **"Diplomatic Play" chooser.**
   - Replace vtable entry `0x00E3440C` (`CMakeCBAction` slot 14) with a wrapper `(owner, view)`. The
     wrapper clones the action and returns a `CEU3Dialog` built on **"DefaultDialog"** (two buttons, two
     shields, title, description: no new `.gui` needed), with our vtable copy.
     - Agree, "Justify Casus Belli": call `0x00926D00` with the clone, `vt[0x28]`, then `GuiOwner_AddDialog`.
     - Decline, "Initiate Treaty": open the treaty editor.
   - Button text: the game's loader reads the vanilla `MAKE_CB_BUTTON` first, and our CSV loader only
     serves our own keys. So the DLL sets the text itself (`Button_SetText`) after
     `CDiplomacyView_RefreshActions` (`0x0062B620`), which re-texts the buttons on every refresh, with a
     key of our own (`SE_DIPLOMATIC_PLAY_BUTTON`). **[I]**
2. **Treaty editor (sender).**
   - Our own `CEU3Dialog` on a window type in `mod/script_extender/interface/se_treaty.gui`. It is a
     copy of `SetupPeaceDialog` (same sprites, so the same look) plus:
     - two term lists: "we demand" and "we give";
     - two "add term" buttons;
     - a money `scrollbarType` copied from `transfer_slider`, signed or with a direction toggle.
   - Rows use the vanilla `peace_goal_item` type: flags, label, `score` = `WarGoal_PeaceCost`.
   - Slot 1 (per frame) writes the total and the AI's "will / will not accept" from our evaluator, as
     `CPeaceDialog_Update` does.
   - Slot 6 sends `action->vt[0x18]()`.
   - Load the `.gui` by redirecting the `Gui_Finalize` call at `0x0063690D`: parse our files with the
     per-file sequence (`FileReader_ctor`, `ParserBlock_ctor`, `(g_gui+4)->vt[0xC]`), then call the
     original. `.gfx` goes into `app+0x11C` before that. Texture paths
     `mod/script_extender/gfx/interface/...` resolve from the PhysFS root. **[I, high]**
3. **Add term.** Open the vanilla `CWarGoalDialog` (`DeclareWarDialog`). For "we give", swap actor and
   target. Then:
   - give that one instance its own copy of the vtable `0x00E356C4`;
   - redirect the `FillCbList` call (`0x0093FD58`): for a treaty instance, list the `is_treaty_cb` CBs
     whose `can_use` passes for the pair, using the scopes of `CBManager_IsCBUsable` (casus-belli.md);
     otherwise call the original;
   - the state list and its `allowed_states` filter stay vanilla. If `WarGoal_IsPossible` refuses states
     in peacetime **[?]**, replace `FillStateList` for treaty instances (call sites `0x00944A50`,
     `0x00944D00`);
   - slot 6 (OnAgree): read the CB (`clone+0x34`) and the province (`clone+0x50`), append a `CWarGoal`
     to the editor's action, close, and post no command;
   - slot 1: call the original, then enable AgreeButton (outside a war `CanPerform` is false).
4. **Recipient.** Nothing to build: base slot 13 opens the DefaultDialog box. The text is slot 16. A
   human answers with PROCEED / DECLINE; an AI answers through our evaluator.

### 4. Validation (slot 36, also re-run in Execute)

| check | why |
|---|---|
| not at war with each other (slot 26) | treaties are a peacetime tool |
| every term: `WarGoal_IsValid(goal, 0, 0, 0)` and `can_use` | the CB's own rules |
| the province or state is still owned by the giver, and not occupied by a third country | `TransferProvinces` overwrites the controller |
| no term would annex: not the giver's last province or state, no `po_annex` | `po_demand_state` annexes a last state, and `TransferProvinces` leaves a country with no provinces half dead |
| the payer's treasury ≥ the amount, at Execute time | money is checked when applied, not when sent |
| at least one term | |

### 5. Execute (slot 32, on every machine)

1. Validate again (section 4). If anything fails, apply nothing and post a message.
2. Sort the terms by the CB `peace_order`, then call `WarGoal_Execute(goal)` for each one. Its prestige
   transfer and the CB's `on_po_accepted` run too: a mod sets `prestige_factor = 0` on a treaty CB to
   avoid the prestige.
3. Money: `payer.treasury -= amt; payee.treasury += amt` (`CCountry+0xE78`). Optionally book it in the
   ledgers (expense category 10, income `+0x38`, through `g_budgetSlot`). Do not use
   `Country_SpendMoney`: it borrows.
4. Repair, for both countries:
   - `Country_ValidateCapital`;
   - `Military_RepositionArmies(world+0xAB4, c)`;
   - `Economy_CalculateDistribution` when territory moved;
   - `AI_DailyUpdate`.
5. Options to decide:
   - infamy: `WarGoal_Infamy` of the demanded terms, added to `+0x1430`;
   - a truce (`relation+0x6C`, the CB's `truce_months`);
   - the relation change on accept (slot 22).

All of this reads only synced state. `po_disarmament` uses the synced game RNG.

### 6. AI evaluation (proposal)

The peace rule is `accept iff cost ≤ theirWarscore + W/2`. Without a war there is no war score, so the
treaty rule weighs what the AI gives against what it gets:

```text
get    = Σ WarGoal_PeaceCost(terms the AI receives) / 1000 + money_in  / MONEY_PER_POINT
give   = Σ WarGoal_PeaceCost(terms the AI gives)    / 1000 + money_out / MONEY_PER_POINT
accept = get - TREATY_GIVE_WEIGHT * give + TREATY_OPINION_FACTOR * AI_GetOpinionScore(ai, other)
         - TREATY_BASE_RELUCTANCE >= 0
```

- `TREATY_GIVE_WEIGHT` > 1 is the "heavier weight" (for example 2.0).
- `MONEY_PER_POINT` is tied to the AI's income, so money is worth more to a poor country.
- The four values belong in `script_extender.ini`.
- The editor shows the same 0/100 as "will / will not accept".

The province value inside `WarGoal_PeaceCost` already scales with the province (`Province_GetPeaceValue`:
forts, factories, ×3 capital, divided by the owner's total).

## Consequence for `po_demand_province`

`CWarGoalDialog` fills `CWarGoal+0x28` with the **first province** of the chosen state (open question 4 of
casus-belli.md, answered). With the vanilla state list, the player cannot choose which province of the
state `po_demand_province` takes.

**Fixed (2026-10-24, Script Extender `demand_province`, site `se_demand_province_states`):** for a
marked CB the state list walks one stand-in state per province, so the same list shows provinces (see
casus-belli.md, "`po_demand_province`"). A treaty that reuses `CWarGoalDialog` gets it too. A map pick (hook
`CProvince_OnClick` `0x004B17F0`, vtable entries `0x00DFD2B8` / `0x00DFD348`) stays an alternative.

## Risks and open questions

1. A DLL vtable with no RTTI: does any code `dynamic_cast` diplomatic actions? (diplomacy.md, question 2)
2. `WarGoal_IsPossible` (`0x00952230`) rules in peacetime.
3. What a save made with a treaty pending does in a game without the DLL. The unknown token should load
   as `CNullDiplomaticAction` and be dropped **[I]**.
4. Every multiplayer peer needs the DLL. A missing peer reads the token as `CNullDiplomaticAction` and
   goes out of sync.
5. `PeaceOffer_GetTermsText` and `PeaceOffer_GetTotalCost` with a `CTreatyAction`: confirm that neither
   reads the war.
6. Parsing our `.gui` / `.gfx` at the `Gui_Finalize` call (order relative to fonts and textures).
7. Whether `Country_RebuildStates` decrements the giver's state count (`+0xE4C`) when a state empties
   (diplomacy.md, question 4).
8. Money and AI tuning values; whether a treaty should cost infamy or create a truce.
