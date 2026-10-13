# `state_checksum`

## Source

`patch/src/features/multiplayer/state_checksum.cpp`; reverse engineering in
[re/systems/multiplayer.md](re/systems/multiplayer.md).

## Why

The game's daily sync check compares two numbers between host and client: how many random draws were
made on the global generator since the last seed, and the seed. State that diverges without changing
that count (an order applied on one machine only, a rounding difference, a pop that grew differently)
is never reported. The "out of sync" message, when it comes, is usually days after the fact, and the
built-in reaction only realigns the random generator.

## How

- The `CALL DayTick` inside the daily update is redirected to a thunk that runs `DayTick` and then
  appends five values to the world's sync vector, after the two the game put there: a hash of every
  province's owner and controller, a hash of every country's treasury and province count, a hash of
  every pop's id and size, the index of the world's own random generator, and the date.
- The host copies that vector into the next hourly tick command; every client compares it with its
  own, built at the same point of its own day, with the game's unchanged comparison. A difference
  triggers the game's normal reaction (log, RNG realign, dialog, pause).
- The vector is grown with the game's own allocator, once; afterwards the game's daily clear keeps the
  capacity. Hashing is FNV-1a over ~3 000 provinces, ~700 countries and all pops: well under a
  millisecond per day.
- The comparison also fails when the vectors have different lengths, so a host with the option on and a
  client with it off (or the other way round) see "out of sync" on every tick; `oos_log` says so
  explicitly. Every player of a game must use the same value.
- If `tick_profiler` already redirected the `DayTick` call, the hook chains to it.
