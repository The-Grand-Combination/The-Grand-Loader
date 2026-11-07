# Script effects: how the engine builds and runs them, and how to add new ones

This page explains how an effect block (`effect = { ... }`, `option = { ... }`, `on_po_accepted = { ... }`) becomes
objects. It also gives the recipe the Script Extender uses to add effects of its own: the first one is
`dismantle = TAG` (see [treaties.md](treaties.md)).

Sources: this repo's session of 2026-10-25. The sources are the decompilation of the factory `0x00876B30`
and of `CReleaseVassalEffect`, and the exe bytes for every RET.

Confidence: **[C]** confirmed from code, **[I]** inferred, **[?]** guess.

## Summary

- **One effect, one object.** Each keyword in an effect block becomes a C++ object with a 14-slot vtable.
  The object is parsed once, when the file is loaded, and kept for good. Effects are never saved. **[C]**
- **One factory builds the children of a block.** It is `EffectBlock_AddChild` `0x00876B30`: thiscall
  `(block, parser, token)`, RET 8, a switch on the token. Every compound effect (`any_country`, `THIS`,
  `limit` holders, a tag as a scope, ...) has it as **vtable slot 4**. Four places also call it directly:
  `0x00886DF0`, `0x00888B32`, `0x0088E915`, `0x008A2AAB`. **[C]**
- **Running an effect is a virtual call.** Execute is slot 11, `(scope, ?)`, and the tooltip is slot 9. Both
  receive the current `CEventScope`. **[C]**
- **Effects run on every machine** in multiplayer, from synced state, so they must be deterministic. **[C]**

## Entry points

| Entry | Role | Conf |
|---|---|---|
| `EffectBlock_AddChild` `0x00876B30` | builds one child per keyword while a file is parsed: slot 4 of every compound effect, plus four direct calls | C |
| effect slot 7 (`CReleaseVassalEffect_ParseValue` `0x0088DEA0` for the template) | takes the value the parser read | C |
| effect slot 11 (`CReleaseVassalEffect_Execute` `0x0088DF10`) | runs the effect with the current `CEventScope`; `WarGoal_Execute` runs a CB's `on_po_accepted` through it | C |
| effect slot 9 (`CReleaseVassalEffect_Tooltip` `0x0088E2D0`) | the tooltip text, evaluated on every hover | C |
| `Token_AddDynamic` `0x009902D0` | registers a new keyword before the files that use it are parsed | C |

## The factory `0x00876B30` **[C]**

1. It switches on `token` and allocates the child. A known effect gets its own class and ctor; for
   example `release_vassal` (`0x700`) allocates 0x2C bytes and calls ctor `0x0088DE60` with EAX = memory.
2. It appends the child to the block's list:
   - `block+0x08` is the head, `+0x0C` the tail, `+0x10` the count;
   - a node is 0x10 bytes `{effect, prev, next, u8}`;
   - the new child is therefore always `*(*(block+0x0C))` right after the call.
3. It copies the value the parser just read (0x104 bytes from `parser+0x228`: the value token, then
   its text) onto the stack and calls the child's **slot 7** with it.
4. **The default case** (an unknown keyword) tries the keyword as a country tag (`FUN_004DC420` /
   `FUN_004DACC0`), then as a region, and so on; that is how `ENG = { ... }` becomes a scope effect.
   Anything else is logged as an unknown effect.

Entry prologue: `55 8B EC 83 E4 F8` (push ebp; mov ebp, esp; and esp, -8). These 6 bytes have no relative
or relocated operand, so a trampoline can copy them. **[C]**

## The effect object and its vtable

Common fields, as the simple ctors set them **[I]**:

| Offset | Field |
|---|---|
| +0x00 | vtable |
| +0x04 | `0x18D` |
| +0x08 / +0x0C / +0x10 | the child list of a compound effect (zero in a simple one) |
| +0x14 | u8 |

**`CReleaseVassalEffect`** (vtable `0x00E2B4AC`, 0x2C bytes) **[C]**:

| Offset | Field |
|---|---|
| +0x1C | u8: `= THIS` |
| +0x1D | u8: `= FROM` |
| +0x20 / +0x24 | `= TAG` as `{tag, index}`; `---` / 0 when none |
| +0x28 | u8: the "random vassal" form (token `0x70C`), or a tag that does not resolve |

**Vtable slots.** The addresses are those of `CReleaseVassalEffect` (C = confirmed for this class):

| Slot | Address | Role |
|---|---|---|
| 0 | `0x0087A3F0` | dtor (shared) |
| 4 | `0x00876B30` | the child factory above (`OnKeyword`) |
| 7 | `0x0088DEA0` | **parse the value**: thiscall `(token, 0x100-byte text)`, RET 0x104 **[C]**. Token `0x405` (`THIS`) sets +0x1C, `0x3DE` (`FROM`) sets +0x1D, `0x70C` sets +0x28; anything else is looked up as a tag (`FUN_0055C6C0` on `g_countryManager`), into +0x20/+0x24 |
| 8 | `0x0087A6C0` | shared (29 callers) **[?]** |
| 9 | `0x0088E2D0` | **tooltip**: thiscall `(std::string* out, CEventScope* scope, std::string by value, int)`, RET 0x28 **[C]**. `out` is uninitialised memory that receives the result; the function returns `out`. The callee destroys the string it got by value (it frees the buffer when capacity > 15) |
| 10 | `0x00928300` | empty string (shared) |
| 11 | `0x0088DF10` | **Execute**: thiscall `(CEventScope* scope, ?)`, RET 8 **[C]** |
| 12 | `0x00A31490` | returns 1 (shared; "persist" in globals.csv) |

`CAddCountryModifierEffect` (`0x00E2ABE0`) has the same layout: slot 4 is its own parse
(`0x00881260`), slot 9 its tooltip, and slot 11 its Execute.

**Resolving the target.** `CReleaseVassalEffect` uses `0x0088E620` (EAX = effect, ESI = out
`{tag, index}`, stack = scope) **[C]**:
- **THIS:** the THIS scope (`scope+0x24`): its country `+0x08/+0x0C`, or else the owner of its province
  `+0x10`.
- **FROM:** the THIS scope's `+0x2C/+0x30`, or else the owner of its province `+0x34`.
- **Otherwise:** the stored tag.

## `CEventScope` (0x48 bytes, ctor `0x008A8650`)

| Offset | Field | Tag |
|---|---|---|
| +0x08 / +0x0C | country `{tag, index}` of the scope | C |
| +0x10 | province id (read for THIS when there is no country) | C |
| +0x18 | province id (the province scope of `WarGoal_GetTargetStates`) | C |
| +0x24 | `CEventScope*` THIS | C |
| +0x2C / +0x30 | FROM country `{tag, index}` | C |
| +0x34 | FROM province | C |

Where the FROM fields are read matters. Effects and many triggers read FROM from the THIS scope.
WarGoal_IsValid puts it only there, while nested scopes copy the scope itself (see treaties.md, "Scopes").

**Example: a CB's `on_po_accepted`** (`cb+0x1AC`, run by `WarGoal_Execute` through slot 11) **[C]**:
- the scope's country is the goal's receiver (the loser);
- THIS is the goal's actor (the holder);
- the war still exists at that point.

## Recipe: a new effect

This is how the Script Extender adds `dismantle = TAG` and `dismantled_by = TAG` (`treaty.cpp`, "Dismantling"):
one template, one vtable copy per keyword, and the vtable tells them apart in Execute.

1. **Register a dynamic token.** Call `Token_AddDynamic` `0x009902D0`: cdecl
   `(const std::string* name, bool allowExisting)`, which returns the id. It must happen before the files
   that use the token are parsed. The plugin registers its tokens right before `CBManager_Load`
   (the call at `0x0063DCD2`), which comes before events and decisions **[I]**: the dismantle effect in
   `cb_types.txt` itself is read inside that call, after the registration.
2. **Hook the factory entry.** In the Script Extender this is done once, by `game/effects.cpp`
   (`game::add_effect_builder`): each feature adds a builder that sees every token first and builds its
   own (`game::build_template`, `game::copy_effect_vtable`). On its own:
   - Use `hooks::make_trampoline(0x00876B30, 6)` plus `hooks::jump_hook` to a
     `__fastcall (block, edx, parser, token)` function.
   - Return what the trampoline returns, and pass every other token through.
   - A hook on the four direct calls would miss the vtable slot 4 of every compound effect, so it must be
     the entry.
3. **Build a template effect.** For your token, call the trampoline with the token of an existing
   effect whose value parse fits. For example `release_vassal` (`0x700`) takes `TAG`, `THIS` or `FROM`;
   `set_country_flag` (`0x40B`) keeps any text as a `std::string` at `+0x20` (`change_flag`, [flags.md](flags.md)).
   The factory allocates, appends and parses it as usual.
4. **Swap the vtable.**
   - Read the new child from the block's tail (`*(*(block+0x0C))`), and check that it changed and
     that it has the template's vtable.
   - Point it to a static copy of the template's vtable, with slot 9 (tooltip) and slot 11 (Execute)
     replaced.
   - Keep the RTTI locator at `[-1]`.
   - The object's size and fields stay the template's, so slot 0 (dtor) and slot 7 (parse) still fit.
5. **Write Execute** as `void __fastcall (effect, edx, CEventScope* scope, int)`: callee-cleaned, which
   matches RET 8.
   - Read your fields (for example the resolved target) and the scope's country.
   - Use synced state only, and never the UI or the local player, except to decide what this machine
     shows.
6. **Write the tooltip** as `uintptr_t __fastcall (effect, edx, uintptr_t out, CEventScope* scope,
   GameString by_value, int)`.
   - Build `out` from nothing: zero it, set capacity 15, then `std::string::assign` `0x00409350`.
   - Free `by_value`'s buffer when its capacity is over 15, and return `out`.
   - Tooltips are evaluated often (on every hover over a decision or event option), so keep them cheap:
     cache any text read from files.
7. **Effects that need peace or a quiet moment.** An effect can run in the middle of a peace
   (`on_po_accepted`) or of an event. When it needs the war over, queue the work and take it after
   `CPeaceAction` slot 32 (`0x00E344F4`) returns, as the dismantle does.

**Pitfalls:**
- **A parse slot that is not slot 7.** A template whose parse is not slot 7 takes the value differently.
  `add_country_modifier`, for example, parses in slot 4 with its own `name` / `duration` keys (a block
  value), so its children never reach the factory. Look at the template's ctor and slots first.
- **A token parsed before the hook.** If the token is not yet registered when a file is parsed, the
  factory's default case logs it as unknown and drops it.
- **Dynamic token ids.** They differ between installations and between runs. Only compare them with the
  id returned at run time.

## Patch sites

| id (`patch_sites.csv`) | site | what |
|---|---|---|
| `se_effect_factory` | `0x00876B30`, entry of `EffectBlock_AddChild` | one hook for every new effect (`game/effects.cpp`): the dismantle keywords build a `release_vassal` template, `change_flag` a `set_country_flag` one; each gets the plugin's vtable |
| `se_treaty_peace_execute` | `0x00E344F4`, `CPeaceAction` slot 32 | the work queued by the effect is taken after the peace |
| `cp_add_province_modifier_tooltip` / `_execute`, `cp_add_country_modifier_tooltip` / `_execute`, `cp_remove_province_modifier_tooltip` / `_execute`, `cp_remove_country_modifier_tooltip` / `_execute` | slots 9 and 11 of the four modifier effects | `fix_missing_modifier`: a null modifier gives an empty tooltip and a skipped Execute |
| `cp_clr_country_flag_slot`, `cp_clr_global_flag_slot` | slot 11 of `CClrCountryFlagEffect` / `CClrGlobalFlagEffect` | `cull_decision_checks` notices a cleared flag |

## Open questions

1. The role of slot 8 (`0x0087A6C0`, shared by 29 callers) **[?]**.
2. The second argument of Execute (`(scope, ?)`).
3. The common fields `+0x04` (`0x18D`) and `+0x14` of the simple effects are read from the ctors only **[I]**.
4. An in-memory parser source (`FileBuffer_ctor` `0x009D5450`) would let a plugin read effects without a
   generated file; not done.
