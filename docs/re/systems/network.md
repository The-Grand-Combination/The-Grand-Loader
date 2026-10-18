# Network layer (ENet) and the per-frame wait

Sources: this repo's sessions (2026-10-11), found with `sample_profiler` v3.

## Summary

- The game links **ENet** statically (`enet_host_service` `0x00A89D00`, `enet_socket_wait` `0x00A8B400`,
  identified by ENet's own error strings and by `select` / `WSARecvFrom` imports at `0x00C8A330` /
  `0x00C8A338`).
- Every frame `CommandQueue_Process` services the session object (`[obj+0x38]->vt[0x1C](0)` at
  `0x009AFBB1`), which ends in `NetSession_Service` (`0x00A61780`):
  `enet_host_service(this+0x18 host, &event, this+0x20 timeout)` at `0x00A618CC`. **[C]**
- The base constructor (`0x00A5FF00`) sets the timeout to **15 ms**, the client/server variants
  (`0x00A635C0`, `0x00A63830`) to 35 ms. **[C]** A single-player session logged 15 ms on its first poll. **[M]**
  This also happens in single player, where the session is a local loopback.
- `enet_host_service` sends, receives and dispatches, then blocks in `select()` until a packet arrives
  or the timeout ends. **[C]**
- Measured (TGC 1836, max speed): the game thread spends **~46 ms per game day blocked in
  `ntdll!ZwWaitForSingleObject` under `select`**, i.e. ~23 % of the day. **[M]** (`sample_profiler` v3,
  2026-10-11; all of the 46 ms is under the `0x00A618CC` call.)

## Chain (sampled, checked instruction by instruction)

```text
CInGameIdler_Update
  CommandQueue_Process (0x009AF9F0)       CALL EAX at 0x009AFBB1   ([this+0x38]->vt[0x1C](0))
    ...                                    (frames in between not identified)
      NetSession_Service (0x00A61780)     CALL at 0x00A618CC
        enet_host_service (0x00A89D00)    CALL at 0x00A89E43
          enet_socket_wait (0x00A8B400)   CALL [select] at 0x00A8B477
            ws2_32 / mswsock -> ntdll!ZwWaitForSingleObject
```

## Patch: `skip_network_wait` (on by default)

`patch/src/features/performance/network_wait.cpp` redirects the `0x00A618CC` call and forwards it with a timeout of
0. Every packet is still received and dispatched; at most it waits for the next frame's poll.

1. **Does the day advance faster?** Yes: −20 to −22 % on the first year (below).
2. **Is the wait the game's idle mechanism when paused?** No: with the option on, CPU use still drops
   sharply when the game is paused (checked 2026-10-11).
3. **Multiplayer:** the redirected call is the server's per-frame service only. Clients service ENet
   from 15 other call sites (`0x00A63B2B` ... `0x00A69857`), which keep their waits. Two-instance games
   ran with it on; a join that hung (2026-10-13) was not reproduced with it off, and it stays a suspect.

## Result (2026-10-11)

TGC, new game 1836, max speed, culls off, `engine_worker_threads = 0`, `sample_profiler = 1`:

| `skip_network_wait` | first year | ms/day | ntdll (sampled) |
|---|---|---|---|
| 0 (vanilla, three runs) | 1 min 11–14 s | 195–203 | ~50 ms/day |
| 1 | **56 s** | **155** | 5 ms/day |

The game's own functions kept their exclusive times, so the simulation does the same work; only the
idle wait is gone. With both culls as well: 52 s, 144 ms/day (−26 %).

## Ports and addresses (2026-10-17)

- **Two UDP ports, both fixed:** **1638** (`0x666`) for the lobby session and **1633** (`0x661`) for the
  game session the client opens once the game starts. Every connection request is built with
  `0x666` (`JoinIpDialog_OnJoin` `0x0076727B`, the other lobby paths `FUN_00767C30`, `FUN_007680B0`,
  `FUN_007A73B0`, the session config `FUN_00A04BB0`). **[C]** Where 1633 comes from is not pinned down:
  `FUN_00693DF0` (from `Game_FinalizeLoadAndEnterGameplay`) stores `0x661` at `+0x84` of an object it
  sets up, which fits but is not traced to the connect. **[I]** No TCP is used. Seen live with two instances on one LAN: the host bound UDP
  `0.0.0.0:1638`; the client connected to `host:1638` in the lobby, then to `host:1633` after loading.
  A host behind a router needs both ports forwarded; a tunnel needs both.
- **Connection request:** `ConnectRequest_ctor` (`0x009F8180`, cdecl) copies the name ("LobbySession"),
  a second string (+0x24), the host string (+0x40) and the port (+0x68). `NetClient_ctor` (`0x00A635C0`)
  resolves the host with `gethostbyname`, then `inet_addr`, so names work and `host:port` resolves to
  nothing and connects nowhere without an error. `join_port` splits `address:port` in the join dialog;
  it covers the lobby connection only, not the game connection on 1633.
- **`SessionManager_Start`** (`0x009AEB90`) creates the client or server from a request passed by value;
  the three calls in `CInGameIdler_Update` build the local "InitialSession" ("NoAdress") on the way
  back to the menu, not a reconnection.
- **Connect pattern:** both sides log CONNECT, DISCONNECT about 0.03-0.25 s later, CONNECT again for every
  connection (lobby and game). Seen in every run, working ones included; the peer address is already
  cleared (`0.0.0.0:0`) when the disconnect event is returned.

## ENet event and peer layout **[C]**

`enet_host_service` (`0x00A89D00`, cdecl (host, event, timeout), > 0 with an event; prologue
`55 8B EC 83 E4 F8`) fills ENet's event: type `+0x00` (1 connect, 2 disconnect, 3 receive), peer
`+0x04`, packet `+0x10`. The peer follows the ENet 1.2 layout: host pointer at `+0x00`, address at
`+0x0C` (IPv4, network order) and port at `+0x10` (host order) — the server's
`NetSession_OnPeerConnect` (`0x00A61410`) copies the same two fields into its peer table.
`network_log` hooks the function's entry and logs connects, disconnects and silences over 2 s.

## Tunnels (playit, 2026-10-17)

With the tunnel's agent on the host's PC, every remote player reaches the host from the agent's LAN
address (the host's own IP), and the public address has a port chosen by the service. The lobby worked
through `join_port`; the game never started (host "Waiting for players", client "Waiting for host")
most likely because the client's game connection went to the tunnel's address on 1633, where nothing
listened **[I]** (no connect or disconnect for it was logged before the instances were closed). A
tunnel needs a second endpoint for 1633 and the client must dial its port; not implemented.
