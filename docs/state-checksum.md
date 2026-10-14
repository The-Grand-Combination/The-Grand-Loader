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
  appends five values to the world's sync vector, after the two the game put there: the global
  generator's draw count after the day (`rng_calls_end`, the host's position when a client compares;
  used by `fix_rng_resync`), a hash of every province's owner and controller, a hash of every
  country's treasury and province count, a hash of every pop's id and size, and the date.
- The host copies that vector into the next hourly tick command; every client compares it with its
  own, built at the same point of its own day. The game's own comparison is limited to its two
  elements (`sync_check.cpp`), so it reacts as in vanilla; the appended elements are compared by the
  patch and reported by `oos_log` as a silent divergence. (Letting the game react to them was tried
  first and made things worse: its realign restarts the generator, see `fix_rng_resync`.)
- Nothing is hashed with fewer than two players in the game (nobody would compare). The vector is
  grown with the game's own allocator, once; afterwards the game's daily clear keeps the capacity. Hashing is FNV-1a over ~3 000 provinces, ~700 countries and all pops: well under a
  millisecond per day.
- A host with the option on and a client with it off (or the other way round) is reported by
  `oos_log` as a settings mismatch; the game itself still only sees its two elements.
- If `tick_profiler` already redirected the `DayTick` call, the hook chains to it.
