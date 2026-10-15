# `fix_missing_modifier`

## Source

`patch/src/features/stability/modifier_guard.cpp`.

## Why

An `add_country_modifier`, `add_province_modifier`, `remove_country_modifier` or
`remove_province_modifier` effect names a modifier from `common/event_modifiers.txt`. When the name
is unknown the parser writes a line to the game's own log and leaves the effect's modifier pointer
null. Executing the effect is harmless: the add effects return on a null pointer and the remove
effects find nothing to remove. The tooltip builders are not: when the event or decision that
holds the effect is shown, each asks the modifier for its name through a virtual call on the null
pointer, and the game crashes. A typo in a mod's events crashes the game the first time the event
window opens, long after the log line went by.

## How

- The lookup call in both parse functions is redirected to a wrapper that calls the original and,
  on a miss, writes the modifier's name and the effect family to `the_grand_loader.log` (the first
  fifty, so a broken mod cannot flood the log).
- The tooltip slot of the four effect vtables goes to a guard: with a modifier present it jumps to
  the original; with none it returns an empty tooltip through the same path the original uses when
  it has nothing to say (initialise the output string, release the string argument passed by
  value).
- The execute slot of the four vtables goes to a guard that logs "executed, nothing done" for a
  null modifier and returns, which is what the game does silently; with a modifier present it
  jumps to the original.
- An effect whose modifier exists runs the unchanged code on every path.
