# `mp_host_wait_days`

## Source

`patch/src/features/multiplayer/host_wait.cpp`; reverse engineering in
[re/systems/multiplayer.md](re/systems/multiplayer.md).

## Why

Only the host issues the hourly ticks, and it issues them as fast as its own frame allows. Every
machine reports its date to the host once per tick; the host reacts only when a player is six days
behind ("X is lagging behind, decreasing speed") or eleven (pause). The softer reaction above two
days changes how often the host renders inside a tick and does not slow the ticks at all. So at the
highest speed a slower client falls six days behind again and again, and the game keeps lowering
the speed and showing the message. With the Performance Patch the host is faster than vanilla,
which makes the gap open sooner.

## How

- The per-frame call to the function that issues ticks is redirected to a thunk. On the host, it
  reads the last date each player reported (the same entries the game updates from the ping
  commands) and skips the call while the slowest player is more than `mp_host_wait_days` days
  behind; the host then waits instead of running ahead. On clients, and in single player, nothing
  changes.
- A player whose reported date stops changing for 15 seconds (a disconnect) is left out of the
  check, so it cannot hold the host; the game's own player-lost handling takes over.
- The first ten waits are logged with the lag seen.
