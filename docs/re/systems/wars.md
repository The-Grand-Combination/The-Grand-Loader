# Wars and crises: how a war starts, who joins it, how a crisis is made

This page covers the gates and the code that create a war, add countries to one, and build and fill a
crisis. Diplomatic actions in general
(classes, slots, the gate) are in [diplomacy.md](diplomacy.md).

Sources: this repo's session of 2026-10-25 (decompilation, exe bytes for every RET and patch site, V2Dump
disassembly for the crisis manager). Confidence: **[C]** confirmed from code, **[I]** inferred, **[?]** guess.

## Summary

- **One test decides "can declare war"**: `War_CanDeclare` `0x0093AA90`. Its callers are the action's
  CanPerform, which the buttons and the AI use through the generic gate, and a war tooltip. **[C]**
- **The `war` effect and the console skip that test.** They call `CDeclareWarAction_Execute` directly. **[C]**
- **Every way into a war ends in `War_AddAttacker` / `War_AddDefender`.** That includes the declaration
  itself, call to arms, crisis sides and the join commands. Both return at once for a country already on
  that side. **[C]**
- **Puppets are protected by the vassal rules, not by a flag.** A country that is a vassal or substate
  cannot be declared on (`War_CanDeclare` with "needs CB"). The TGC mod's neutrality uses that by making
  neutral countries puppets of the dummy tag `U02`. **[C]**
- **There is no single crisis predicate.** A crisis is created in `CrisisManager_DayTick`. Countries enter
  it through two tests, `Crisis_CanJoinInterest` and `Crisis_CanBackSide`. **[C]**

## Declaring a war

| Address | Name | Convention | Role |
|---|---|---|---|
| `0x0093AA90` | `War_CanDeclare` | cdecl `(actor ref*, target ref*, bool needsCb)` → AL | **[C]** fails for: a vassal/substate target (with needsCb); the same sphere (`FUN_00534000`); an alliance (`relation+0x28`); either in the crisis (`FUN_0042B220`); military access not from the overlord (`relation+0x20`); a truce (`relation+0x70`); already at war (`country+0x7B4` wars); no usable CB (needsCb, `CBManager_IsAnyCBUsable`). `world+0xD11` (a debug flag **[I]**) makes it true. |
| `0x0093AE10` | `War_CannotDeclareReason` | cdecl `(std::string* out, actor ref*, target ref*, bool)` → out | **[C]** the localised reason, the same tests |
| `0x00915DD0` | `CDeclareWarAction_CanPerform` | slot 36 | **[C]** `War_CanDeclare(actor, recipient, 1)`, then the war goal (`WarGoal_IsValid`) and no token `0x8DA` in the CB's list |
| `0x00915CB0` | `CDeclareWarAction_GetCannotReason` | slot 37 | **[C]** `War_CannotDeclareReason`, else `ACT_WARDESC` |
| `0x0093B950` | `War_CanDeclareTooltip` | | **[I]** one line per country of a list, with the reason |
| `0x00913510` | `CDeclareWarAction_Execute` | thiscall `(ctx)`, RET 4; slot 32 (`0x00E34314`) | **[C]** see below |
| `0x00885CA0` | `WarEffect_Execute` | thiscall `(scope)` | **[I]** the `war = {}` effect: builds the action (`0x009134B0`), calls the Execute directly (`0x00885E2C`), then adds its goals only if `relation+0x34` (the war) is set |
| `0x00427FD0` | console `war` | | **[I]** calls the Execute directly (`0x00428143`) |

The calls of `War_CanDeclare` are at `0x00915DE7` and `0x0093B9E8`. The calls of the reason are at
`0x00915DA9` and `0x0093BAF5`.

**`CDeclareWarAction_Execute`** **[C]**:
- It does nothing when `relation+0x34` already holds a war.
- Infamy for the goal (`country+0x1430`).
- It creates the `CWar` (0x10C bytes, `FUN_009318B0`) and appends it to the world list (`world+0xB3C..0xB44`).
- It calls `War_AddAttacker(actor)` and `War_AddDefender(recipient)`.
- With the crisis flag (`action+0x69`), it adds the crisis leaders and both crisis sides.
- With `action+0x68` ("call allies"), it sends a `CCallAllyAction` to each ally (`country+0xD58`). Each
  call goes through the gate first.
- Then come the `AGRESSOR` / `DEFENDER` messages and the `WAR_JUST` / `WAR_INJUST` texts.

## Joining a war

`War_AddAttacker` `0x00934280` and `War_AddDefender` `0x00935390` **[C]**:
- Convention: stdcall `(war, bool, bool notify, tag, index)`, RET 0x14.
- Both return at once when the index is already in that side (`war+0x2C` attackers, `war+0x3C` defenders,
  vectors of 8-byte `{tag, index}`).

The direct calls:

| Attacker | Defender | Caller |
|---|---|---|
| `0x00913891`, `0x009139C7`, `0x00913A70` | `0x009138A6`, `0x00913A27`, `0x00913AC0` | `CDeclareWarAction_Execute`: leader, crisis leader, crisis side |
| `0x0091E7C0` | `0x0091E7E7` | `CCallAllyAction_Execute` `0x0091E5E0` |
| `0x005F6BEC`, `0x005F6E5C` | `0x005F709C`, `0x005F730C` | commands `(param bool join/leave)` **[I]** |
| `0x00585B71` | `0x00585C03` | a command with a war index (`+0x44`) and a side (`+0x48`) **[I]** |
| `0x004E7892` | `0x004E78A4` | `FUN_004E7290` **[?]** |
| `0x0093367F` | `0x009337B7` | `FUN_00933560` **[?]** |

`CCallAllyAction` (vtable `0x00E34654`) **[C]**:
- Slot 36 `0x0091EF50`: the caller must be at war (`country+0x67C`), and the called country must be an
  ally (`relation+0x20`), or the caller's vassal, or in its sphere.
- Slot 27 `0x0091E530` (auto-accept): overlord and vassal, in either direction.

Other gates **[C]**:
- `CAllianceAction` (vtable `0x00E26CC4`), slot 36 `0x0091DF80`.
- `CMakeCBAction` (justify war, vtable `0x00E343D4`), slot 36 `0x00927130`.

## Country modifiers

`CHasCountryModifierTrigger` evaluate `0x008CD8C0` **[C]** checks two lists:
- `country+0x4D8`: timed modifiers. Node +0 = entry, entry +8 = modifier, node +8 = next.
- `country+0x4E8`: permanent ones (`duration = -1`). Node +0 = modifier, +8 = next.

The modifier's name is the `std::string` at `+0x30`.

`ModifierRegistry_Find` hashes the name as `h = (h + char) * 0x3D` (signed char) modulo the bucket count
(`registry+4`). The buckets are at `registry+8`, nodes are `{modifier, next}`. `g_eventModifiers` is
`0x0125A69C`.

The script effect `neutrality = yes` (`CNeutralityEffect`, vtable `0x00E2BECC`) only ends alliances and
wars. It leaves no state behind.

## Crises

### Objects **[C]**

- **Crisis manager**: `*(world+0xCF8)`, vtable `CCrisisManager` `0x00DF31A4`.
  - `+0x08`: the current `CCrisis*` (0 when there is none).
  - `+0x0C`: the date the last crisis ended (the cooldown).
  - `CrisisManager_DayTick` `0x0042A130`: stdcall `(mgr)`, called from DayTick at `0x00686240`.
- **`CCrisis`**: 0xB4 bytes, vtable `0x00DF3188`.
  - Daily update `0x00426570`: stdcall `(crisis)`, RET 4.
  - `Crisis_End` `0x0042B270`.

| Offset | Field |
|---|---|
| +0x08 | first province of the target state |
| +0x0C | colonial region (colonial crisis only) |
| +0x10 | temperature, 0..100000 |
| +0x14/+0x18 | attacker leader: the claimant or liberated country (`state+0x284/+0x288`); colonial: the second competitor |
| +0x1C/+0x20 | defender leader: the state owner (`state+0x58/+0x5C`); colonial: the first competitor |
| +0x2C / +0x3C / +0x4C | attacker side, defender side, interested parties (vectors of `{tag, index}`) |
| +0x5C/+0x60, +0x64 | pending backer ask, and whether it is for the attacker |
| +0x68 / +0x78 | queues of Great Powers (GPs) to ask to back the attacker / defender |
| +0x88 / +0x98 | war goals of each side (0x40 bytes each) |
| +0xB0 | the interest on_action already fired |

`Crisis_GetAttackerLeader` `0x00425A40` and `Crisis_GetDefenderLeader` `0x00425AC0` (ECX = crisis,
EAX = out) return the original leader if it is a GP, else the first member of that side if it is a GP.

### Creation, in `CrisisManager_DayTick` **[C]**

1. **Flashpoint** (liberation or claim). It does this only when no crisis exists and the cooldown has passed:
   - It takes the states of each country (`world+0xADC`, `country+0xE44`) with the flashpoint flag
     `state+0x27C` and tension `state+0x280 > 50000`, and keeps the top 3.
   - It rolls a chance from the tension.
   - Neither the owner nor the claimant may be at war.
   - Then it builds the crisis: `CCrisis_CtorFlashpoint` `0x00424F90`, thiscall, RET 0xC, call at `0x0042AB06`.
   - The flag test is at `0x0042A245`: `cmp [edi+27Ch], bl; jz 0x0042A2A5`, with EDI = state and BL = 0.
   - Whether it is a liberation or a claim crisis depends on whether the claimant owns provinces.
2. **Colonial**, only if no flashpoint crisis was made:
   - It takes regions of `g_provinceSource+0x2A88` with `[+0x80] == 2` and `[+0x88] >= 100000`.
   - `FUN_004CD2A0` picks the two competitors.
   - At `0x0042ABEE` (`mov edx, [g_countryManager]`), ECX and ESI hold their indices.
   - Then `CCrisis_CtorColonial` `0x00425270`, call at `0x0042B1F3`.
3. **Backer queues.** Both ctors end in `FUN_00425590`. `Crisis_BuildBackerQueue` `0x004264D0` queues the
   GPs (`world+0xBE0`) for each side whose leader is not a GP.

### Entering a crisis **[C]**

**The two tests:**
- `Crisis_CanJoinInterest` `0x00425B40`: ESI = crisis, EDI = country ref*, plain RET, result in AL.
  - True for a GP that is not a leader, not on a side, not interested yet, and not at war.
  - Its callers: the on_action `on_crisis_declare_interest` (`0x00426D8E`), the effect `add_crisis_interest`
    (`0x0089057E`), the Validate of the `CJoinCrisisInterest` command (`0x00589417`), and the UI (`0x0062DF1D`).
- `Crisis_CanBackSide` `0x00425E20`: EAX = country ref*, EDI = crisis, `(bool attacker)`, RET 4.
  - The same rules, plus `country+0xBB8 == 0` **[?]**.
  - Its callers: the backer asks in `Crisis_ProcessBackerQueue` `0x004278C0` (`0x00427983`, `0x00427A22`;
    on false, the next GP is asked), `CBackCrisisSideAction` CanPerform `0x00928260` (`0x009282D8`), the
    Validate of the `CJoinCrisisSide` command (`0x0058977B`), and the UI (`0x0062ECB0`).

**What adds a country to a side, with no check of its own:**
- `Crisis_JoinAttacker` `0x00428D70` / `Crisis_JoinDefender` `0x00428E50`, stdcall `(crisis, country*)`.
  Callers: the Execute of `CJoinCrisisSide` and of `CCrisisOfferAction`.
- `Crisis_BackerResponse` `0x00427B70`.

**Actions:**
- `CCrisisOfferAction` (vtable `0x00E346F4`).
  - Slot 36 `0x00927E40` checks only the cooldown and the war goal. It never checks the recipient.
  - Slot 32 `0x009278A0` adds the goal before the join.
- `CCrisisBackDownAction` (vtable `0x00E34514`).
  - Slot 36 `0x0091CA90`.
  - Slot 32 `0x0091B1D0` ends the crisis.

The command Validate (`0x00911290`) runs the gate, and so slot 36, on every machine.

### Not covered

- A crisis loaded from a save.
- The console `crisis` command (`FUN_0042B470`, `0x00425610`).
- Who writes `state+0x280`.

## Hook points for a neutrality feature (designed, not implemented)

A feature keeping countries with `has_country_modifier = neutrality` out of wars would hook (built and tested
to compile on 2026-10-25, then left out):

| Place | Hook |
|---|---|
| `War_CanDeclare`, `War_CannotDeclareReason` | both calls of each: false / `SE_NEUTRALITY_US` or `_THEM` |
| `CDeclareWarAction_Execute` | slot 32 and the two direct calls: nothing between a neutral and anyone |
| `War_AddAttacker` / `War_AddDefender` | all 18 calls: a neutral is skipped when the side already has someone |
| slots 36 / 37 of alliance, justify war, call ally, crisis offer, crisis back down | false / the reason |
| `0x0042A245`, `0x0042ABEE` | a flashpoint state with a neutral owner or claimant, or a colonial crisis with a neutral competitor, is skipped |
| `Crisis_CanJoinInterest`, `Crisis_CanBackSide` | entry detours: false for a neutral |
