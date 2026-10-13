# `oos_log`

## Source

`patch/src/features/multiplayer/oos_log.cpp`.

## Why

The game's own "Checksum:" lines go to its log with bare numbers. To act on a desync one needs to know
which part of the state differed first, and whether both sides even run the same settings.

## How

- `sync_check.cpp` owns the hook on the game's comparison and hands both vectors to this reporter
  before the game sees them. The game itself only compares its own two elements; the rest is compared
  here, so a difference in a state hash is logged as a silent divergence and does not trigger the
  game's reaction.
- On a difference it writes one `OUT OF SYNC` (the game reacts) or `SILENT DIVERGENCE` (it does not)
  line with the game day, and one line per differing element with its name (`rng_calls`, `rng_seed`,
  then `state_checksum`'s `rng_calls_end`, `provinces`, `countries`, `pops`, `date`) and both values. Different vector lengths are reported as a settings
  mismatch between host and client.
- The first 20 mismatching ticks are logged in full, then one line every 30 game days.
- On the first comparison it also logs the game thread's x87 control word and MXCSR, to see the
  floating-point precision the simulation runs at.
