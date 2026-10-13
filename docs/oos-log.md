# `oos_log`

## Source

`patch/src/features/multiplayer/oos_log.cpp`.

## Why

The game's own "Checksum:" lines go to its log with bare numbers. To act on a desync one needs to know
which part of the state differed first, and whether both sides even run the same settings.

## How

- The `CALL Vector_Equals` in the daily update, the one comparison between the local sync vector and
  the host's, is redirected to a thunk that compares the two vectors itself, logs, and then runs the
  original call unchanged.
- On a difference it writes one `OUT OF SYNC` line with the game day, and one line per differing
  element with its name (`rng_calls`, `rng_seed`, then `state_checksum`'s `provinces`, `countries`,
  `pops`, `world_rng`, `date`) and both values. Different vector lengths are reported as a settings
  mismatch between host and client.
- The first 20 mismatching ticks are logged in full, then one line every 30 game days.
- On the first comparison it also logs the game thread's x87 control word and MXCSR, to see the
  floating-point precision the simulation runs at.
