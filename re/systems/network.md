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

`patch/src/features/network_wait.cpp` redirects the `0x00A618CC` call and forwards it with a timeout of
0. Every packet is still received and dispatched; at most it waits for the next frame's poll.

1. **Does the day advance faster?** Yes: −20 to −22 % on the first year (below).
2. **Is the wait the game's idle mechanism when paused?** No: with the option on, CPU use still drops
   sharply when the game is paused (checked 2026-10-11).
3. **Multiplayer:** open. The wait may be part of how the client paces itself on the host's commands;
   test a local multiplayer game with every player on the same setting.

## Result (2026-10-11)

TGC, new game 1836, max speed, culls off, `engine_worker_threads = 0`, `sample_profiler = 1`:

| `skip_network_wait` | first year | ms/day | ntdll (sampled) |
|---|---|---|---|
| 0 (vanilla, three runs) | 1 min 11–14 s | 195–203 | ~50 ms/day |
| 1 | **56 s** | **155** | 5 ms/day |

The game's own functions kept their exclusive times, so the simulation does the same work; only the
idle wait is gone. With both culls as well: 52 s, 144 ms/day (−26 %).
