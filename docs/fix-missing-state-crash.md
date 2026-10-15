# `fix_missing_state_crash`

## Source

`patch/src/features/stability/state_guard.cpp`.

## Why

Every owned province belongs to a state object; a province without an owner has none, and the
pointer in the province is null. The AI's daily target scan walks the country's units and, for
each one, takes the state of the province it stands in and asks two helpers about it: the
controller of the state and whether anything hostile is in it. Both read the state at once. Most
other callers of the helpers test for a state first; this one does not, because a unit cannot
normally be in a province without an owner. A mod that removes the owner of provinces while armies
stand in them (an "uncolonise" effect) breaks that assumption, and the next AI scan crashes reading
the null state. Seen in a user's log: access violation reading `0x0000000C` at `0x0048E126`, from
`0x00851C6D` inside the scan.

## How

- The prologue of each helper is replaced by a jump to a guard. With a state the guard replays the
  prologue and continues in the original. Without one, the presence check returns "nothing
  hostile" and the controller lookup returns the answer the game itself gives for a state without
  an owner (tag `---`, index 0).
- The first twenty occurrences are logged, so a mod that triggers it can be told.
- Any call with a real state runs the unchanged code.
