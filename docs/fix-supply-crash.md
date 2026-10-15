# `fix_supply_crash`

## Source

`patch/src/features/stability/supply_guard.cpp`.

## Why

The per-country factory walk that feeds the production view reads, for every factory, the
local-supply source of its production type: the state object the type draws its input from. A
production type whose state has no such source carries a null pointer there, and the game reads
through it without a check. Opening the production view of such a country crashes with an access
violation at offset `0x128` of a null pointer. V2DLL reported the crash on 3.04; the read sits at
`0x004D15EB` in `FUN_004D1560`.

## How

- The six-byte read is replaced by a jump to a guard. With a source present the guard replays the
  read and continues where the game would have. With none it jumps past the three calls that use
  the source, to the employment count that follows them, which does not depend on it. The stack is
  at the same level at both points (the callees clean their arguments), and nothing stored in the
  skipped part is read later.
- Only the factory without a source is affected; every other factory, and every country whose
  factories all have one, runs the unchanged code.
