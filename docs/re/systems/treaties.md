# Treaties (Script Extender `treaty`)

**Status: built (2026-10-24), testing in progress.** ini key `treaty = 1` in `mod/script_extender.ini`.
Code: `script_extender/src/features/treaty.cpp` (the action, validation, execution, AI) and `treaty_ui.cpp`
(loading, the chooser, the editor, the texts). Engine background: [diplomacy.md](diplomacy.md) and
[casus-belli.md](casus-belli.md). Addresses are in [functions.csv](../symbols/functions.csv); hook sites in
[patch_sites.csv](../symbols/patch_sites.csv) (`se_treaty_*`).

Two countries at peace trade terms (provinces, states, spheres, puppets, tributes, ...) and money. The
other side accepts or refuses, as with a peace offer. Every multiplayer peer needs the plugin.

## Design

1. **Terms are CBs.** A CB with `is_treaty_cb = yes` is a treaty term: its peace options applied in
   peacetime. It keeps its usual fields (`peace_cost_factor`, `badboy_factor`, `prestige_factor`,
   `can_use`, `allowed_states`, `allowed_countries`, ...). A term goes either way: demanded or offered.
2. **Money** is an extra term, a lump sum either way.
3. **Entry point.** The diplomacy view's "Justify war" button opens a chooser ("Diplomatic Play"):
   **Justify Casus Belli** (vanilla) or **Initiate Treaty** (the editor).
4. **The recipient** gets the vanilla incoming box, as for a peace offer. The AI answers with its own rule.

## Architecture

| piece | how | conf |
|---|---|---|
| the proposal | `CTreatyAction`: a `CPeaceAction` object with another vtable, so it travels, waits in the pending list, is saved and is answered as vanilla actions are | C |
| its type id | the dynamic token `treaty_action` (slot 8) | C |
| the registry | entry hook on `DiplomaticAction_CreateByType` `0x0090E850`: our token builds a `CTreatyAction`. Covers MP commands, saves and the UI | C |
| the terms | `CWarGoal`s in the offer's `war_goal` vector (`offer+0x3C`), all selected (`offer+0x1C` flags = 1). Each has its own actor (gains) and receiver (gives) | C |
| the recipient | base slot 13 (`DiplomaticAction_ShowIncoming` `0x00913020`): the DefaultDialog box, title slot 7, text slot 16 | C |
| the AI | hook on the type switch of `AI_ProcessPendingProposals` | C |
| keywords | `is_treaty_cb`, `treaty_action`, `se_treaty_money` registered with `Token_AddDynamic` at the `CBManager_Load` call | C |
| treaty CBs | the plugin's `common/se_treaty_cbs.txt`, appended to the game's (or mod's) `cb_types.txt` | C |
| screens | the plugin's `interface/se_treaty.gui` / `.gfx`, parsed with the game's | C |

### `CTreatyAction` **[C]**

- Made by `CPeaceAction_CtorDefault` `0x00916A30` (stdcall `(this, date, actorTag, actorIdx, recTag,
  recIdx)`, RET 0x18) in **0xA0** bytes. Then the vtable is replaced and `+0x98` zeroed.
- `+0x28` the embedded `CPeaceOffer`; `+0x44` = `offer+0x1C` (flags); `+0x64` = `offer+0x3C` (goals).
- `+0x98` i64 money, 2^15 fixed point: the actor pays the recipient when > 0, the recipient pays when < 0.
- The vtable is a copy of `CPeaceAction_vftable` (38 slots). The RTTI locator at `[-1]` is the one of
  `CDiplomaticAction`, so a cast to `CPeaceAction` does not take a treaty for a peace offer.

All slots are thiscall. Overridden or replaced slots:

| slot | value | does |
|---|---|---|
| 2 | ours | `CPeaceAction` WriteFields `0x00919660`, then `se_treaty_money = "<i64>"` when not 0: `CmdStream_WriteToken` `0x009A0940` + `CmdStream_WriteString` `0x0099F770` (thiscall `(std::string*, stream, 0)`, RET 8) |
| 4 | ours | `se_treaty_money` → `+0x98` (`strtoll` of `parser+0x22C`); anything else → `CPeaceAction` OnKeyword `0x00919690` |
| 7 | ours | title `SE_TREATY_TITLE` (`(out)` RET 4) |
| 8 | ours | the `treaty_action` token |
| 9 | ours | name `"SE_TREATY"` (`(out)` RET 4): base of the message keys |
| 12 | ours | Clone: `CPeaceAction_CopyCtor` `0x00916B50` (stdcall `(dst, src)`, RET 8: copies the offer, runs `PeaceOffer_SetupSides`, no `FillFromWar`) + the money |
| 13 | `0x00913020` | base: the DefaultDialog box for a human recipient |
| 14 | `0x00912E70` | base CreateDialog; never reached (the action is not in the view's table) |
| 16 | ours | the offer text (`(out)` RET 4): `SE_TREATY_OFFER` + `PeaceOffer_GetTermsText` `0x00929480` (thiscall `(offer, std::string* out)`, reads no war) + a money line |
| 17 | `0x00633620` | returns 0 (no third parties **[I]**) |
| 18 / 19 | `0x0092C720` / `0x0092C700` | base SetRecipient / SetActor: no war lookup |
| 20–23 | `0x00424B40` | returns 0x15B: no diplomatic or influence cost, no relation change |
| 26 | `0x00A31490` | 1: not while at war with the recipient |
| 27 | `0x0058CFD0` | 0: never auto-accepted |
| 28 | `0x00A31490` | 1: diplomat travel time applies |
| 32 | ours | Execute (`(ctx)` RET 4) |
| 34 | ours | OnDecline (`(ctx)` RET 4): only makes sure the message texts exist |
| 36 | ours | CanPerform (`(bool)` RET 4): the validation below is empty |
| 37 | `0x00928300` | empty "cannot send" reason (`(out, int)` RET 8) |

`ctx` of slots 32 / 34 is the message-token context `Message_MakeContext` `0x005061E0` builds for the
player country **[I]**.

### Terms **[C]**

A term is a `CWarGoal` built with `CWarGoal_Ctor` `0x0094BDB0` (EAX = this, ECX = cb, stack `(actorTag,
actorIdx, recTag, recIdx)`, RET 0x10):

| field | treaty meaning |
|---|---|
| `+0x0C` | the treaty CB |
| `+0x10/+0x14` | the third country (CBs with `allowed_countries`), else unset |
| `+0x18/+0x1C` | actor: the side that gains |
| `+0x20/+0x24` | receiver: the side that gives |
| `+0x28` | the chosen province (CBs that ask for one), else 0 |

## Hooks

| id | site | original | change |
|---|---|---|---|
| `se_treaty_create` | `0x0090E850` entry of `DiplomaticAction_CreateByType` | `55 8B EC 83 E4 F8` | JMP + trampoline: `treaty_action` → new `CTreatyAction` |
| `se_treaty_ai_switch` | `0x00820EBB` type switch of `AI_ProcessPendingProposals` | `8B 06 8B 50 20 8B CE` | JMP thunk: our vtable → chance 0 / 100, then JMP `0x0082102B` (`CMP EAX,0x32`) |
| `se_treaty_keyword` | `0x00E33734` `CCasusBelliType` slot 4 | `E0 A1 90 00` | chained after `demand_province`: `is_treaty_cb = yes` marks the CB |
| `se_treaty_load` | `0x0063DCD2` CALL `CBManager_Load` | `E8 E9 F0 2C 00` | chained: registers the tokens, passes the merged CB block |
| `se_treaty_gfx` | `0x00635D89` end of the `.gfx` loop | `8B 55 C0 8B 8A 28 01 00 00` | JMP thunk: parses the plugin's `interface/*.gfx` |
| `se_treaty_gui` | `0x0063690D` CALL `Gui_Finalize` (`0x009A4190`) | `E8 7E D8 36 00` | parses the plugin's `interface/*.gui` first |
| `se_treaty_messagetypes` | `0x0063C4AA` CALL `MessageTypes_Read` (`0x006A4FA0`) | `E8 F1 8A 06 00` | then reads `interface/se_messagetypes.txt` |
| `se_treaty_chooser` | `0x00E3440C` `CMakeCBAction` slot 14 | `00 6D 92 00` (`0x00926D00`) | the "Diplomatic Play" chooser |
| `se_treaty_button` | `0x0062B9FC` CALL `DiplomaticAction_CanPerform` in `CDiplomacyView_RefreshActions` | `E8 5F 5D 2E 00` (`0x00911760`) | ESI = action; for `CMakeCBAction` (vtable `0x00E343D4`) the button is enabled when a treaty is possible |

If `se_treaty_create` does not match, the feature stays off. If a screen site does not match, it stays off
too. `se_treaty_ai_switch` failing only makes the AI refuse every treaty (vanilla `default` = 0).

## Files

| file | role |
|---|---|
| `mod/script_extender.ini` | `treaty`, `treaty_infamy`, `treaty_ai_*` (Diplomacy section) |
| `mod/script_extender/common/se_treaty_cbs.txt` | the 11 treaty CBs (below) |
| `mod/script_extender/common/se_cb_types.generated.txt` | written at every start: the `cb_types.txt` the game would read + `se_treaty_cbs.txt` |
| `mod/script_extender/interface/se_treaty.gui` | `se_treaty_dialog` (eu3dialogtype): the editor |
| `mod/script_extender/interface/se_treaty.gfx` | `GFX_se_treaty_bg`, `GFX_se_treaty_tab` (2 frames), `GFX_se_treaty_row` |
| `mod/script_extender/gfx/interface/se_treaty_{bg,tab,row}.tga` | the editor's textures (edited copies of `diplo_makecb_bg`, `diplo_peacetab`, `diplo_wargoal_button`) |
| `mod/script_extender/interface/se_messagetypes.txt` | the six answer message types, copies of `PEACE_*` |
| `mod/script_extender/localisation/00_script_extender.csv` | `SE_TREATY_*`, the message texts, the CB names |

### The treaty CBs

| CB | copy of | peace options | asks for |
|---|---|---|---|
| `treaty_cede_state` | – (new) | `po_demand_state` | a province (its state), not colonial, not the capital's state |
| `treaty_cede_province` | – (new) | `po_demand_province` (needs `demand_province = 1`) | a province |
| `treaty_cede_colony` | – (new) | `po_demand_state` | a colonial province |
| `treaty_pay_tributes` | `war_reparations` | `po_reparations` | nothing |
| `treaty_install_government` | `install_communist_gov_cb` | `po_install_communist_gov_type` | nothing |
| `treaty_add_to_sphere` | `add_to_sphere` | `po_add_to_sphere` | nothing (the receiver itself joins) |
| `treaty_make_puppet` | `make_puppet` | `po_make_puppet` | nothing |
| `treaty_take_from_sphere` | `take_from_sphere` | `po_add_to_sphere` | a sphere member of the receiver |
| `treaty_release_puppet` | `release_puppet` | `po_release_puppet` + `po_add_to_sphere` | a puppet of the receiver |
| `treaty_free_peoples` | `free_peoples` | `po_transfer_provinces` | a country with cores, then one of its states |
| `treaty_liberate_country` | `liberate_country` | `po_transfer_provinces` + `po_remove_cores`, `all_allowed_states` | a country with cores |

All have `is_triggered_only = yes`, `constructing_cb = no`, `truce_months = 0`, `prestige_factor = 0`,
`penalty_factor = 0` and `can_use` with `NOT = { war_with = THIS }`.

## Screens

### "Diplomatic Play" chooser **[C]**

- `CMakeCBAction` slot 14 returns a `CEU3Dialog` on the vanilla **"DefaultDialog"** with our vtable copy.
  It keeps a clone of the action (`+0x68`) and the view (`+0x6C`).
- Agree = "Justify Casus Belli": `CMakeCBAction_CreateDialog` `0x00926D00` with the clone, `vt[0x28]`,
  `GuiOwner_AddDialog`. It is disabled when the vanilla `DiplomaticAction_CanPerform` fails (truce,
  relations, ...).
- Decline = "Initiate Treaty": the editor.
- The view's button is enabled when a treaty is possible (both exist, not the same, not at war), whatever
  the CB rules say (`se_treaty_button`).
- The button text stays `MAKE_CB_BUTTON` (`CDiplomacyView_MakeAction` keeps 0x931, whose alternative is also
  0x931). TGC renames it "Diplomatic Play" in its own localisation; the plugin does not.

### The editor **[C]**

A `CEU3Dialog` on `se_treaty_dialog`, our vtable copy: slot 0 dtor, 1 per-frame refresh (only when dirty),
6 send (`CanPerform`, then `action->vt[0x18]()` queued through `owner->vt[0x44]() → +0x38 → vt[0x18]`), 7
cancel. Layout:
- left: the terms, 8 rows (`se_term_<i>` buttons + `se_term_icon_<i>` icons), paged; the money first, then
  the CB terms; click = remove;
- right: Demand / Offer tabs (`se_mode_demand` / `se_mode_give`, frame 2 active, 1 inactive, as
  `CPeaceDialog_Update` does), then a picker of 6 rows (`se_pick_<i>`), paged, with Back;
- bottom: `score` ("we gain X, we give Y" in war-score points), `acceptance` (the AI's answer, or why the
  treaty cannot be sent), AgreeButton (enabled when sendable), DeclineButton.

**Picker rules** (the war-goal dialog's, generalised):
1. Money (always the first item), then each treaty CB that is supported and whose `can_use` passes for
   (gains, gives). A CB that needs nothing more is listed only if the term would be valid.
2. Money → an amount (100 … 100 000) the payer can afford.
3. A CB whose `allowed_countries` has children → choose a third country: every country except the two,
   evaluated per candidate (scopes below).
4. A CB whose `allowed_states` has children and not `all_allowed_states`, or with `po_demand_state` → choose
   a province of the giver; each candidate is checked as a full term.
5. Otherwise the term is added at once.

**Widget calls used:** window finders `0x34` button, `0x3C` text box, `0x44` icon; buttons `0x34` show,
`0x38` hide, `0x48` frame, `0xB8` / `0xBC` enable / disable, `0xCC` SetText; icons share `0x34` / `0x38` /
`0x48` (`Icon_vftable` `0x00E3A930`: `Icon_Show` `0x009C43C0`, `Icon_Hide` `0x009C43F0`, `Icon_SetFrame`
`0x009C4410`); a CB's icon frame is `cb+0x0C` (`sprite_index`) on `GFX_diplo_wargoal_icons`. Text boxes:
assign `tb+0xDC` with `0x00408ED0`, then `TextSprite_SetText`. Clicks: one button glue (0x2C bytes,
vtable `0x00E086E0`) per editor; its slots null-check their handler, and the handler at `+0x20` receives
`(button)`. Shields: `0x00641630` / `0x006416A0`.

**A window type must exist before `CEU3Dialog_ctor`:** `owner->vt[0x34]()` (the GUI) `+0x34` → `vt[4](name)`
returns 0 for an unknown type, and the ctor would crash on it. The plugin checks first.

## Validation **[C]**

`treaty_problem` (slot 36, the editor, the AI and again in Execute). The first failure is the reason
(`SE_TREATY_*` text):

| check | reason key |
|---|---|
| both countries exist (own provinces), not the same | `GONE` |
| not at war with each other (`relation+0x34`) | `AT_WAR` |
| at least one term or money | `NO_TERMS` |
| the payer's treasury (`+0xE78`) covers the money | `MONEY_SHORT` |
| per term: a treaty CB; valid (`goal_valid_scoped`, below) | `TERM_INVALID` |
| per term: between the two countries, either way | `TERM_SIDES` |
| per term: no `po_annex`, no `po_colony` | `TERM_ANNEX` |
| `po_demand_state` / `po_transfer_provinces`: the giver keeps its capital's state and ≥ 1 state. Target states = `WarGoal_GetTargetStates` when `all_allowed_states`, else the chosen province's state | `TERM_LAST_STATE` |
| a chosen province is owned and controlled by the giver, which has more than one state | `TERM_OWNER`, `TERM_OCCUPIED`, `TERM_LAST_STATE` |

When a term is added the editor also refuses a second term on the same state, or the same term twice
(`TERM_DUPLICATE`): a second one would find the map already changed.

`goal_valid_scoped`: without a third country, `WarGoal_IsValid(goal, 0, 0, 0)` (which returns true after
its checks when war and crisis are 0, `0x00952D1B`). With one, the plugin evaluates as the peace's
execution does (see "Scopes"): `allowed_countries` with FROM in both scopes, then `allowed_states` per
target state in a province scope, each state still owned by the giver.

## Execution (slot 32, every machine) **[C]**

1. `treaty_problem` again; on failure nothing is applied (logged).
2. Per selected term, re-checked (an earlier term may have moved the map): with `treaty_infamy = 1` the
   gaining side gets `WarGoal_Infamy` added to `+0x1430`; then `WarGoal_Execute`. Its prestige and the CB's
   `on_po_accepted` run too.
3. Money: payer `+0xE78 -= amount`, payee `+= amount`. No ledger, no loan.
4. For both countries, the repairs `CPeaceAction_Execute` runs (`0x009183F8`): `Country_ValidateCapital`;
   while the game runs, `Military_RepositionArmies(world+0xAB4, c)`, `Province_UpdatePortBlockade` per
   controlled province, `Units_UpdateHostileFlags`; `Economy_CalculateDistribution` when a term was
   applied; `AI_DailyUpdate`.
5. No truce, no relation change (slots 20–23 = none).

### What `WarGoal_Execute` does with a treaty term **[C]**

| option | without a third country | with one |
|---|---|---|
| `po_add_to_sphere` | the receiver joins the actor's sphere | the third leaves the receiver's sphere (influence reset, `MAX_INFLUENCE` define `+0x404`) and joins the actor's (`Sphere_Remove` / `Sphere_Add`) |
| `po_release_puppet` | nothing | `Vassal_Release(&third, &receiver)` |
| `po_make_puppet` | the receiver's vassals are released, then a new `CVassal` actor / receiver | |
| `po_install_communist_gov_type` (0x975) | the receiver takes the actor's government type (`Country_SetGovernment` `0x00513A90`) and joins the actor's sphere if the actor is a great power; the counter war goal for war allies needs a war | |
| `po_transfer_provinces` | | the target states go to the third country, released if it does not exist |

## AI evaluation

On the host, `AI_ProcessPendingProposals` → our thunk → `ai_chance`: 0 when `treaty_problem` fails, else
100 when the score ≥ 0 or `yesmen` is on. The game then rolls `rand() % 100` against it (0 / 100 decide).
The editor shows the same answer for an AI recipient. A human recipient (`world+0xAEC[c] != 0`, as
`CIsAiTrigger_Evaluate` `0x008D7450`) answers in the box.

Score of the AI `self` against the proposer `other`, in war-score points × 100:

```text
cost(term) = WarGoal_PeaceCost(term) / 10
gain  = Σ cost of terms self gains (× core_gain% when the province is a core of self) + money received
give  = (Σ cost of terms self gives (× core_give% for its cores) + money paid) × give_weight%
money in points = pounds / money_per_point
relation = relation(self, other) / RELATION_MAX × relation
           if relation < 0 and ratio ≥ coerce_ratio%: relation = -relation × hostile%
ratio = (strength(other) + 1) × 100 / (strength(self) + 1)
power = min(max(ratio - 100, 0), power_cap) × power      (× neighbour% when they border)
score = gain - give + relation + power - reluctance × 100
```

- `strength(c)`: `Units_SumStrength` `0x005DCBD0` (stdcall `(c+0x7B4, int* land, int* naval)`, it **adds**
  to the outputs), force = land × 3 + naval (the peace offer's 0.75 / 0.25 balance).
- Neighbours: `country+0xD08` u8 indexed by the other country (`CNeighbourTrigger_Evaluate` `0x008D4620`).
- Cores: `province+0x148/+0x14C` vector of `{tag, index}` (`CIsCoreTrigger_Evaluate` `0x008BDE30`).
- `RELATION_MAX` is `0x0131B1E8`.

| ini key | default | meaning |
|---|---|---|
| `treaty_ai_reluctance` | 10 | points the AI wants on top of a fair deal |
| `treaty_ai_give_weight` | 150 | percent: what it gives counts this much |
| `treaty_ai_money_per_point` | 1000 | pounds per war-score point |
| `treaty_ai_relation` | 8 | points at the best relation (minus at the worst) |
| `treaty_ai_core_gain` / `_core_give` | 2000 / 4000 | percent: worth of a core province it gets / gives |
| `treaty_ai_power` | 5 | points per 100% the proposer is stronger |
| `treaty_ai_power_cap` | 300 | percent of advantage counted at most |
| `treaty_ai_neighbour` | 200 | percent of the power term when they border |
| `treaty_ai_coerce_ratio` | 300 | from this strength ratio a bad relation helps the proposer |
| `treaty_ai_hostile` | 100 | percent of the relation points it then gives |
| `treaty_infamy` | 1 | a term costs its CB's infamy, as a war goal |

**`yesmen`** (console, string `0x00DF213C` used at `0x00422229`) toggles `g_cheatAIAcceptAll` `0x012589A7`;
the treaty AI accepts everything then. Log line:

```text
treaty: AI <r> accepts|refuses <a>: gain G - give V + relation R + power P - reluctance K = S[ (yesmen)]
```

## Answer messages **[C]**

- `CDiplomaticActionCommand_Execute` builds the message key from slot 10 (if non-empty) else the slot 9
  name, plus `_THEY_ACCEPT` / `_WE_ACCEPT` / `_OTHER_ACCEPT` (strings from `0x00E33864`, code from
  `0x009107C4`) or the `_DECLINE` analogues. For a treaty: `SE_TREATY_THEY_ACCEPT`, ...
- The six types are added by `interface/se_messagetypes.txt`, read right after `messagetypes.txt` through
  `MessageTypes_Read` (diplomacy.md, "Messages").
- Their texts (`<type>_1`.. `_6`, `_HEADER`, `_LOG`, `_BTN1..3`, `_MAP`, `_SETUP`) go from our CSV into
  the game's localisation (diplomacy.md, "Localisation injection"), before an answer can be shown (at
  `.gui` load, at Execute / OnDecline / AI answer, when the editor opens).

## Loading the treaty CBs **[C]**

- `CBManager_Load` `0x0090CDC0` is stdcall `(manager, block)`. It clears the CB vector and restarts the
  index at 1 first, and deletes the block at the end. **It cannot be called twice** to add CBs.
- So the plugin appends `se_treaty_cbs.txt` to the `cb_types.txt` the game would read and writes
  `common/se_cb_types.generated.txt` (regenerated at every start). The original call gets a block over that
  file (`make_block`); the game's own block is deleted.
- The source is found with the `-mod=` folders (`game::game_file`), else the game folder. `g_searchPaths`
  must not be used: at that moment it points at the game folder even with a mod, so the first version
  merged vanilla `cb_types.txt` into TGC. The CB list then differed from the save's, and TGC saves crashed
  at the first peace (`War_Archive` `0x004678D0`, reading `goal->cb` at `0x00467F4E`).
- The CB names (`name`, `_desc`, `_setup`, `_short`) are injected as the message texts are.

**Is the generated file necessary?** No in principle: `FileReader` buffers the file into an object built
by `FileBuffer_ctor` `0x009D5450` **[?]**, and network commands build a token reader over memory with
`TokenReader_CtorFromBuffer` `0x00990DF0` **[I]**. A block over an in-memory merge would avoid the file.
Not done.

## Loading the `.gui` / `.gfx` **[C]**

Per file, as `EU3App_LoadResources` does: `FileReader_ctor` `0x0098FFC0`, vtable `CTokenReader`
`0x00E37C3C`, `ParserBlock_ctor` `0x009A0BF0` (EDX = name; `(mem, reader, 1)`, RET 0xC), then
`(target+4)->vt[0xC](block)` with target `app+0x11C` (gfx) or `app+0x120` (gui).

- **`.gfx` must be parsed inside the game's `.gfx` loop window** (end of loop `0x00635D89`). Parsed later
  (at `Gui_Finalize`), the sprite's texture at `+0xD0` stays null and drawing it crashes (`Sprite_Draw`
  `0x00A28990` at `0x00A28A6B`).
- `.gui` before `Gui_Finalize` works.
- Texture paths: `TextureManager_LoadFile` replaces `\\` with `/` (`String_ReplaceAll` `0x00989720`) and
  tries `.dds` before `.tga`. Paths are from the game folder (PhysFS root):
  `mod\\script_extender\\gfx\\interface\\x.tga`.

## Modder guide

**Add a treaty CB:**
1. Copy a game CB from `common/cb_types.txt` into `common/se_treaty_cbs.txt` (plugin folder) and rename it
   (a new name: the merged file holds both).
2. Add `is_treaty_cb = yes`.
3. Tune it: `can_use` (keep `NOT = { war_with = THIS }`), `badboy_factor` (infamy with `treaty_infamy`),
   `prestige_factor` (0 avoids the prestige transfer), `peace_cost_factor` (the AI's value).
   `is_triggered_only = yes` and `constructing_cb = no` keep it out of the justify list and the AI's war plans **[I]**.
4. Add CSV texts: `<name>`, `<name>_desc`, `<name>_setup`, `<name>_short`.

A mod can also put `is_treaty_cb = yes` in its own `cb_types.txt`.

**What the fields make the editor ask:**

| CB has | the editor asks |
|---|---|
| `allowed_countries` with conditions | a third country first (FROM in `allowed_countries` / `allowed_states`) |
| `allowed_states` with conditions, no `all_allowed_states` | a province of the giver (its state is the target) |
| `po_demand_state` | a province, even without `allowed_states` |
| `all_allowed_states = yes` | no province: every allowed state of the giver |
| none of these | nothing: the term is added |

Not supported: `po_annex`, `po_colony` (the CB is never listed).

## Scopes (important for modders) **[C]**

| block | scope | THIS | FROM |
|---|---|---|---|
| `can_use` | the giver (target) | the gainer (actor) | unset |
| `allowed_countries` | the giver | the gainer | the candidate third country |
| `allowed_states` | a province scope, `+0x18` = the state's first province, no country | the gainer | the third country |

- This matches the war-goal dialog (`CWarGoalDialog_FillCbList` `0x00940205..0x00940256`) and
  `WarGoal_GetTargetStates` `0x0094E4D0`. casus-belli.md's `IsCBUsable` pseudo-code names may be swapped.
- **The FROM quirk.** `WarGoal_IsValid` and `CWarGoalDialog_FillCountryList` keep FROM only in the THIS
  scope (`+0x2C` tag / `+0x30` index). Nested scopes are copies of the current scope (`EventScope_Copy`
  `0x008A88D0`), and `is_core = FROM` reads FROM from the scope it runs in, so `any_owned_province = {
  is_core = FROM }` never sees it: `free_peoples` / `liberate_country` never pass `WarGoal_IsValid`. The
  plugin puts FROM in both scopes.

## Known limitations and open questions

1. The generated CB file (above): an in-memory block would be cleaner.
2. The answer messages do not list the terms (no `$TERMS$`); only the recipient's box does.
3. The FROM quirk: a vanilla peace with such a goal (`WarGoal_IsValid` in `CPeaceAction_Execute`) would
   fail it; the plugin works around it only for treaties.
4. Untested in game: `treaty_install_government`; the release of a dead country by `free_peoples` /
   `liberate_country`; `treaty_cede_province` without `demand_province`.
5. A save with a pending treaty loaded without the plugin: the unknown token should load as
   `CNullDiplomaticAction` and be dropped **[I]**; the merged CBs would be missing from the CB list, and
   war goals referring to them may break (see the crash above).
6. RTTI: the vtable carries `CDiplomaticAction`'s locator; no `dynamic_cast` on actions was seen **[I]**.
7. The ticking war score and the AI's war-goal value are not used (no war); the AI never proposes treaties.

## `po_demand_province`

`treaty_cede_province` uses `po_demand_province` (Script Extender `demand_province`, casus-belli.md). The
treaty chains onto the same two sites (`0x00E33734`, `0x0063DCD2`), after it.
