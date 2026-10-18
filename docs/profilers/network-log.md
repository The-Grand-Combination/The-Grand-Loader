# `network_log`

## Source

`patch/src/features/multiplayer/net_log.cpp`.

## Why

A multiplayer game can connect, load on every machine and then never start: the host waits for the
players and the players wait for the host. The game's own logs do not say whether a connection was
dropped, and two instances on one machine write the same mod log folder, so their lines mix.

## How

- The entry of `enet_host_service`, through which every packet of every session passes, jumps to a
  wrapper that calls the original and looks at the event it returns.
- Each connection and disconnection is written to the instance's own `the_grand_loader` log with the
  other side's address and the number of peers still connected; so is any silence of more than two
  seconds on a connection, when traffic resumes.
- At most 300 lines per run. Off by default; nothing else changes.
