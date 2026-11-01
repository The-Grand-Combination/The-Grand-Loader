# Triggers, scopes and effects

The script condition/effect machinery shared by events, decisions, CBs and everything else.

Sources: this repo's sessions (2026-10-01..04). Raw material: [raw/trigger-classes.txt](../raw/trigger-classes.txt),
[raw/trigger-vtables.txt](../raw/trigger-vtables.txt), [raw/rtti-vftables.txt](../raw/rtti-vftables.txt).

## Summary

- Every trigger class has an RTTI name (`CTagTrigger`, `CIsAiTrigger`, ...); 236 exist. `Evaluate` is
  vtable slot 6 (`+0x18`): `bool __thiscall Evaluate(scope)`.
- Every top-level block (`potential`, `allow`, `can_use`, event `trigger`, MTTH modifiers) is an
  embedded `CAndTrigger`: `{vftable, 0x18D, vector<CTrigger*> begin/end/cap}`, 0x1C bytes.
- Containers short-circuit in file order. Nothing is cached or reordered, so a block costs exactly
  "children evaluated until the first decisive one". Typical cost: tens of ns per block.
- Lesson from three features: a hook on a call that costs tens of ns cannot win, because the hook
  costs as much as the work. Remove the work from the loop instead.

## Containers **[C]**

| class | vtable | `Evaluate` | semantics | empty block |
|---|---|---|---|---|
| `CAndTrigger` | `0x00DF9994` | `0x008B8260` | false at the first false child | true |
| `COrTrigger` | `0x00E2FB70` | `0x008B88A0` | true at the first true child | false |
| `CNotTrigger` | `0x00E2FBA8` | `0x008B9240` | `NOT (a OR b ...)`: false at the first true child | true |

`CContextTrigger` (`0x00E2FBE0`) is a scope change (`any_*`, `FROM = { }`, `owner`, ...).

## Scope (`CEventScope`, ctor `0x008A8650`) **[C]**

| offset | |
|---|---|
| `+0x08` / `+0x0C` | country reference (tag, index) |
| `+0x10` | province index (province scope) |
| `+0x24` | THIS scope (initialised to self) |
| `+0x30` / `+0x34` | FROM reference |

The ctor writes up to `+0x48`; the event code copies 0x48 bytes (`EventScope_Copy` `0x008A88D0`).

The patch keeps these offsets, the container vtables and the `Evaluate` slot in one header,
`patch/src/game/triggers.h`, which `cull_cb_checks`, `cull_decision_checks`/`parallel_decisions` and
`parallel_events` share.

## Values

- Country references (`tag = X`, `war_with = THIS`, ...) use the parser `0x008D3F50`: literal tag,
  `THIS` (token `0x405`) or `FROM` (`0x3DE`). The flags land at `+0x18` (THIS) and `+0x19` (FROM);
  the literal's country index at `+0x20` (tag chars at `+0x1C`).
- The generic int/float/bool parsers also accept `THIS`/`FROM` and set the same flags, even when the
  class's `Evaluate` ignores them.
- Bool values: `yes` = token `0x29F` (parser `0x008B93B0`). `CIsAiTrigger` (`0x00E30EE8`) keeps it at
  `+0x1C` and tests `world+0xAEC[country] == 0`.

## Classification of the 236 classes

By decompiling every `Evaluate` and looking for `scope+0x24`, `scope+0x30/+0x34`, child evaluation or
scope construction: 174 scalar (read only the scope country, literals and globals), 20
country-reference, 40 pair-dependent, 2 helpers reviewed as scalar. Table:
[raw/trigger-classes.txt](../raw/trigger-classes.txt); generated header used by `cull_cb_checks`:
`patch/src/features/performance/cb_cull_triggers.h`.

## Effects

`CEffect` blocks execute through slot `0x2C` (block) / slot 11 (`+0x2C`) per effect:
`__thiscall Execute(scope, arg2)`, **two** stack arguments, `RET 0x8`. Ghidra's parameter count follows
usage, not the convention: check the `RET imm16` before hooking a virtual (a one-argument hook here
crashed on load).

| effect | vtable | `Execute` |
|---|---|---|
| `CSetCountryFlagEffect` | `0x00E2AC88` | `0x008829E0` |
| `CClrCountryFlagEffect` | `0x00E2ACC0` | `0x00882A40` (flag name std::string at `+0x20`, country = `scope+0x0C`) |
| `CSetGlobalFlagEffect` | `0x00E2AD68` | `0x00882B60` |
| `CClrGlobalFlagEffect` | `0x00E2ADA0` | `0x00882BA0` |
| `CSetProvinceFlagEffect` | `0x00E2ACF8` | `0x00882A90` |
| `CClrProvinceFlagEffect` | `0x00E2AD30` | `0x00882B00` |

## Flags (2026-10-31)

Names and addresses in `symbols/` (`CFlags_*`, `CHasCountryFlagTrigger_*`, struct rows `CFlags`,
`CFlagEntry`, `CProvince+0x328`, `CCountry+0x1B0`).

- **Storage.** A `CFlags` is a tree of 0x20-byte entries `{std::string name, u8 set}`: the global flags
  at `world+0x64`, a country's at `+0x1B0`, a province's at `+0x328`. Lookup is the object's slot 2
  with the name. `set_*_flag` sets the entry (or adds one), `clr_*_flag` writes 0 and keeps the entry.
  **[C]**
- **Save.** The country and the province write `flags = { name = yes ... }` (token 0x40D), set entries
  only, and read it back on load; a province writes the block only when its count is not 0. **[C]**
- **Province effects.** `set_province_flag` / `clr_province_flag` act on the scope's province
  (`CEventScope+0x10`) and set `g_mapView+0x3A28`, the map's dirty flag: the map draws a sprite for
  some province flags. **[C]** effects, **[I]** purpose.
- **`has_province_flag` is missing.** The keyword exists (token 0x43B), but there is no trigger class
  and `TriggerBlock_AddChild` has no case for it. Its default case tries a tag, region or unit scope,
  then logs an unknown trigger and appends nothing. So `has_province_flag = x` is always true, and so is
  `NOT = { has_province_flag = x }`, because an empty `NOT` is true. The vanilla localisation has the
  keys `HAVE_PROVINCE_FLAG` / `HAVE_NOT_PROVINCE_FLAG`, which the exe never references. **[C]**
- **Fix** (Script Extender `province_flags`, sites `se_province_flags_*`): a hook on the factory entry
  builds a `has_country_flag` trigger for 0x43B and gives it a copied vtable:
  - Evaluate looks the name up in the province of the scope, and is false without one, like
    `CTerrainTrigger`;
  - the tooltip is the `has_country_flag` one, but its two key assignments take the province keys
    while it runs for this trigger.

  **[C]**

## Keywords

All script keywords get a token id in `Tokens_RegisterKeywords` (`0x00C381C0`), pattern
`MOV EDX,<string>; ...; MOV ECX,<id>`. To find a keyword: `grep -boa` the string in the exe, convert
the `.rdata` offset to a VA (+0x401A00), `xref` it. Per-class keyword maps:
[raw/cb-keyword-map.txt](../raw/cb-keyword-map.txt), [raw/decision-keyword-map.txt](../raw/decision-keyword-map.txt).
