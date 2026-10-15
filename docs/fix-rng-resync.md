# `fix_rng_resync`

## Source

`patch/src/features/multiplayer/sync_check.cpp`; reverse engineering in
[re/systems/multiplayer.md](re/systems/multiplayer.md).

## Why

When the daily sync check fails, the game reseeds the global random generator and advances it to
the host's position. Before that call it sets the draw count to zero, so the "position" it reaches is
the start of the sequence. Only a client that joined at the very first tick is at that position; for
anyone else the generator is now further away from the host than before. Measured on a two-instance
game: from the day after the first mismatch the client's draw count reads zero at every tick, the
check fails every day, and countries and pops diverge completely within a day. That is why one
"out of sync" in vanilla is never the last.

## How

- Host-vector capture is installed whenever `fix_rng_resync` is enabled, even with
  `state_checksum = 0` and `oos_log = 0`. If capture-hook installation fails, resync stays native.
- The `CALL Rng_Resync` in the mismatch path is redirected to a thunk that first writes the host's
  draw count into the game's counter, then runs the original realign. The count comes from the host's
  vector that was just compared: the element after `DayTick` (`rng_calls_end`, sent by
  `state_checksum`) when the host runs it, which is the host's position at the moment the client
  compares; otherwise the count the host wrote at its day start, one day behind but not zero.
- With both sides on `state_checksum`, a mismatch of the draw count alone (the `Force OOS` test
  button, for instance) is repaired at the next tick and does not come back.
- A mismatch of the state itself is not repaired by this; it is reported by `oos_log` and needs a
  state transfer, which is not implemented.
