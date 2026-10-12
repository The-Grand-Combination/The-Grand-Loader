# `skip_network_wait`

**Default 1.** Polls the network every frame without blocking.
Source: `patch/src/features/performance/network_wait.cpp`. Research: [re/systems/network.md](re/systems/network.md).

## Why

The game links ENet statically. Every frame the session object is serviced through
`enet_host_service(host, &event, timeout)` (call at `0x00A618CC`), which sends, receives and then blocks in
`select()` until a packet arrives or the timeout ends: **15 ms** (35 ms for the client/server variants),
**also in single player**, where the session is a local loopback. At max speed that is ~46 ms of every
~200 ms game day spent waiting for packets that never come.

## How

The `CALL` at `0x00A618CC` goes to a wrapper that calls the original with a timeout of 0. Every packet is
still received and dispatched, at most one frame later.

## Result

First year (TGC 1836, max speed): 195–203 → **155 ms/day (−20 to −22 %)**, with the simulation's own
functions unchanged. CPU use still drops when the game is paused (the wait is not the idle mechanism).

Not tested in multiplayer: the wait may be part of how a client paces itself on the host. Every player
should use the same setting.
