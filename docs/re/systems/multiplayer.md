# Multiplayer: session, command flow, OOS detection and join-in-progress

How a multiplayer game is kept in step, what "Out of Sync" actually checks, what the game does when it
fires, and how a client that joins a running game receives the state. Groundwork for a stability patch
and for a host-pushed resync.

Sources: this repo's sessions (2026-10-13), read in the decompilation and in the executable's bytes.
`[C]` confirmed in the listing, `[I]` inferred, `[?]` guess. [network.md](network.md) covers the ENet
layer under all of this; [tick.md](tick.md) the tick itself.

## Summary

- One **host** runs the server session (`FUN_00a63830` constructor) and every player, host included,
  runs a client-side command queue. Player commands go to the host, which stamps them with the tick
  and rebroadcasts them in one order ("Sending N commands on tick T", `server.cpp:0x87b`). **[C]**
- The day advances only through `CHourlyTickCommand` (`0x32B`), which only the host issues
  (`ProcessGameTimeAdvance`, `0x00682BD0`, `sessionMgr+0x34` is-host check). Clients advance when the
  command arrives. **[C]**
- **OOS check:** the tick command carries a two-element vector `[g_rngCalls, seed]` copied from
  `world+0xB74` at the moment the host issued it. Each receiver compares it with its own vector
  (`Vector_Equals` `0x0068F340`, called at `0x00683036`). The "checksum" is therefore only the number
  of draws made on the global Mersenne Twister since the last seed, not a hash of the game state.
  State divergence that does not change the draw count is **never detected**. **[C]**
- **On mismatch** (`Update_World_and_Advance_Time`, `0x00682EC0`): log two `Checksum:` lines per
  element (`gamestate.cpp` lines 0x630/0x638), reseed the global MT to the host's `(seed, calls)`
  (`Rng_Resync` `0x009B7C80`, called at `0x0068364D`), show the `OOS_TITLE`/`OOS_DESC` dialog once
  (`world+0xBA8`, never reset in a session), send `CPauseGame`, and **keep playing**. The RNG is
  realigned, the rest of the state is not. **[C]**
- **Join in progress exists.** On an ENet connect the server pushes a message `0x12` holding every
  "setup" command it kept (`sessionMgr+0xD8` list) to the new peer, which enters state 6, loads, replays
  the commands received meanwhile and reports "REQUESTING SYNCHED". The same primitive can push the
  host's state to a peer that is already in the game. **[C]** (what the setup list holds for a running
  game: **[I]**, see below)

## Session objects

| object | how to reach it | notes |
|---|---|---|
| session manager | `(world+0xB24)->vt[0x44]()` | `+0x2C` state, `+0x34` is-host (1 in single player and for the host, `FUN_009AF160`), `+0x38` session, `+0x54` tick counter, `+0x70` list of new peers, `+0xD8/+0xDC/+0xE0` setup-command list {head, tail, count}, `+0x110` received state buffer, `+0x234` executing flag |
| session (ENet) | `sessionMgr+0x38` | base vtable `0x00E426A8`, server/client vtable `0x00E427C8`; slot 6 (`+0x18`) = queue/send a command, slot 7 (`+0x1C`) = service (the `enet_host_service` wait of network.md), slot 27 (`+0x6C`) = push the setup list to a peer (`FUN_00A60630`) |
| peers | `session+0x18` host, peer array stride `0xE8` | `FUN_00A603D0` maps a player id to a peer index ("Sending targeted asynchronous command") |

Session manager states seen: 0 idle, 3 error, 4 in game, 5/7/8/9/0xD/0xE lobby and transfer states,
6 synching (joined a running game, state received, not applied yet), 0xB/0xC in-game variants.
`CommandQueue_Process` (`0x009AF9F0`) services the session only in states 2, 4, 0xB, 0xC. **[C]**

Session messages handled by the client (`FUN_00A674C8`, `switch` on the first word): 0 initial,
2 add peer, 3 player connected (`FUN_009B0F50`), 4/5/7/0xB/0xF/0x10 state changes, **0x12 game
state** (`+0x110` = buffer, state 6), 0x13 own id, 0x14 → state 9, 0x15, 0x19 kick. **[C]**

## Commands

- Every command derives from `CCommand` (vtable `0x00DEDA5C`, 16 slots): 2 serialize, 4 deserialize,
  6 `Execute`, 7 type id, 12 "keep for joiners" predicate (`return 1` stub `0x00A31490` on
  `CSetRandomSeed`, `CSelectBookmark`, `CSetHistoryDate`, `CAddPlayer`, `CPlayerConnected`, `CPauseGame`,
  `CSetGamePlayOptions`, `CResetGame`; `return 0` stub `0x0058CFD0` on `CHourlyTickCommand`,
  `CStartAICommand`), 13 clone, 14 validate. The wire format is the script text format: fields are
  written as tokens (`FUN_0072FBD0` writes the checksum vector as `{ %d %d }`), read back by field id
  (`0x344` for the vector, `FUN_005E5A20`). **[C]**
- Base fields: `+0x2C`/`+0x30` type id, `+0x34` word tick stamp (set by the server from
  `sessionMgr+0x54`), `+0x36` "send immediately / targeted" flag (`FUN_00A66B30`). **[C]**
- `CommandQueue_Process` appends every executed command whose slot-12 predicate is true to the setup
  list (`0x009AFDE7`–`0x009AFE3F`), after cloning it (slot 13). **[C]**
- 74 gameplay command classes plus the session ones (`CQuit`, `CSelectBookmark`, `CClearBookmark`,
  `CResetGame`, `CSetGamePlayOptions`, `CSetHistoryDate`, `CAddPlayer`, `CPlayerConnected`,
  `CSetRandomSeed`, `CPauseGame`, `CAutosave`, `CStartGame` `0x41D`, `CClientPingCommand`). **[C]**

## Which machine runs what

- **Host only:** ticks; creation of the per-country AI objects (`country+0x208`, list `world+0xA4`)
  in `FUN_00687610`, `FUN_00687A70`, `FUN_006881D0`, `FUN_0068B6C0` (all behind the is-host check),
  hence the per-frame AI jobs (`AI_CountryFrameJob`: decisions, construction, military) and the
  AI's choice of event options (`Event_Fire` `0x008A8120` at `0x008A8240`: needs `+0x208`, draws
  from the event manager's own MT, sends the option as a command). Also `FUN_004A46E0`,
  `FUN_00595520`, `FUN_00729850`. **[C]** The AI's commands reach the clients like any player's.
- **Every machine, in lockstep:** `DayTick` and everything in it, including `AI_DailyUpdate`
  (`0x0085CCB0`, state embedded at `country+0x364`), which executes decisions directly
  (`AI_DiplomacyUpdate` `0x0085D270`, `ai_will_do` roll, `vt[0x30]` of the decision's effect).
  `EventManager_DailyScan` fires events on every machine; only the option choice is host-side. **[C]**
- `world+0xD11` is **tutorial mode** (`FUN_0077B800`), not "is client": it gates the AI bucket fill
  and the event scan only in the tutorial. **[C]**

## Random number generators

Several MT19937 instances, all with the same `Rng_Next` (`0x009B7610`, state in `EAX`, index at
`state+0x9C0`): **[C]**

| instance | seeded by | counted in `g_rngCalls`? | used by |
|---|---|---|---|
| global `g_mtState` `0x00F0ECF0` | `CSetRandomSeed` (clock at Play, `FUN_0072CB40`), `Rng_Resync` | yes: the wrappers `FUN_009B7950/7AC0/7D30/7D80` and the inlined loops in `DayTick` (1000 draws), `EconomyManager_PrepareUpdate`, `EventManager_DailyScan` all `INC [0x00F20C14]` | the simulation |
| world `world+0xB4` (index `+0xA74`) | clock in the world constructor (`FUN_0067F790`), or save key token `0x7A4` on load (`FUN_006817F0`); index reset to 0 both times | no | twenty call sites in the AI code (list in `world_rng.cpp`), among them `FUN_0085CF10`, the daily "does this AI country think today" roll, on every machine; the Lua binding `GetAIRand` (`0x008687E0`, unused) |
| event manager (`DAT_01319B80`, `+0x58`) | `__time64` at creation (`FUN_008A76A0`) | no | `Event_Fire`'s AI option choice (host only) |
| AI / unit objects (`FUN_007EC640`, `FUN_008108E0`, `FUN_007810A0`) | various | no | AI (host only) and effects |
| second global `0x00F0E040` (index `0x00F0EA00`) | the constant 1234, reset at every `EconomyManager_PrepareUpdate` (`0x0068DAE5`) | no | `FUN_009599A0` from the economy update and `Pop_MonthlyUpdate`: deterministic on every machine |

Because every global-MT draw is counted, `(seed, calls)` fully describes its position and the
built-in resync does restore it. The world MT is the one to watch: seeded from the save but with
the index reset, and read in lockstep code.

`random.log`: the console toggle `FUN_0041E9D0` ("Random Logging started/stopped", flag
`DAT_01258A65`) logs every wrapper draw with its caller (`random.cpp` lines 0xF6/0x10D) and every
command executed by `CommandQueue_Process` (`0x213`/`0x228`). Comparing two machines' logs is the
intended way to find where they diverge. **[C]**

## Join in progress

1. ENet connect on the server (`FUN_00A61410`, from `NetSession_Service` at `0x00A61B3C`): announce the
   peer to the others (message 2), machine id (7/1), tick (4), then `session->vt[0x6C](peer)` =
   `FUN_00A60630`: serialize the setup list into one message `0x12` and `enet_peer_send` it to that
   peer only; finally `FUN_009B0F50` queues the peer in `+0x70`. **[C]**
2. Client: message `0x12` → `+0x110` = buffer, state 6. The in-game lobby update (`FUN_00697510`, per
   frame from `CInGameIdler_Update`) eventually calls `FUN_009B07A0`: open the buffer as a stream,
   `FUN_009B1900` reads it token by token, creates each command through the factory (`vt[8]`) and
   executes it (`vt[0x18]`), processing the command queue every 5; then executes the commands received
   meanwhile (`+0xD8`), sets state 4, logs "REQUESTING SYNCHED". **[C]**
3. The setup commands of a game started from the lobby include `CSetHistoryDate`/`CSelectBookmark`
   (or a save name: the lobby checks `File_Checksum` of its local copy against the host's and either
   loads it, `LOADING_LOCAL_SAVE`, or starts a transfer, `TRANSERING_SAVE`, `CStartFileTransfer`
   `0x158`, `largefile.cpp`, chunked "Appending #N") and `CSetRandomSeed(seed, 0)`. **[C]** For a game
   that has been running for a while this would not reproduce the current state; what the host puts in
   the list in that case (an autosave plus a transfer, or nothing) is **open** [?]. The joiner's
   `g_rngCalls` starts at 0, so its first tick compare fails and the OOS path realigns the global MT.
4. Host-side UI for arrivals and departures: `CPlayerConnected` → `FUN_00694E20` ("PlrArrived",
   `ALL_PLAYERS_HERE`) / `FUN_00695950` ("Player left"), in-game lobby `ingamelobby.cpp`. A lost
   player gives the host the `PLAYER_LOST` dialog (`CMultiplayerHandler`, buttons continue / save /
   quit / `CONT_AI` "Replace w. AI"); a lost server gives clients `SERVER_LOST` ("quit and rehost").

## Causes of desync (ranked by what the code allows)

1. **Undetected state divergence.** The check is a draw counter. Anything that mutates state on one
   machine without changing the count (a UI action that writes state directly, a local-only pass, a
   float rounding difference) stays invisible until it happens to change a count, and by then the
   state has drifted for days.
2. **Uncounted RNGs read in lockstep code.** The world MT (`world+0xB4`) is the confirmed case: its
   seed comes from the save but its index restarts at 0 on every load, so a joiner or a reload runs
   `FUN_0085CF10`'s roll from a different position than the host.
3. **The in-game RNG realign after OOS** fixes the MT and nothing else, so the first OOS is usually
   followed by more as the diverged state produces different counts.
4. **Timing-dependent paths.** Every machine runs `DayTick` deterministically, but anything keyed on
   wall time inside it is a risk; `__time64`/`timeGetTime` are used by the lazily created managers'
   seeds (event manager) and by the AI frame budget (host only). Not exhaustive.
5. **Floating point.** The CRT sets 53-bit x87 precision at start (`_controlfp_s(0x10000, 0x30000)`);
   Direct3D 9 is created with `0x40`/`0x20` only (hardware/software vertex processing,
   `Render_InitRestore`), so **no `D3DCREATE_FPU_PRESERVE`**: the device creation lowers the main
   thread to 24-bit. Worker threads keep 53-bit. Any `float10` (x87) arithmetic that runs on a
   worker in one run and on the main thread in another can round differently. This matters for the
   patch's `parallel_decisions`/`parallel_events` and for the engine's own TBB sections. **[C]** for
   the flags, **[I]** for the consequence: to verify by reading the control word on each thread.
6. **Different files / mods / checksum**: handled by the lobby checksum; out of scope.

## Floating point and the C runtime

Checked on 2026-10-13 while looking for sources of divergence outside the game's own logic. **[C]**
unless marked.

- **CRT math dispatch.** The statically linked MSVC 2010 runtime picks an implementation on every call
  of its math functions: `if (__sse2_available /*DAT_013F3AB8*/ && (MXCSR & 0x7F80) == 0x1F80 &&
  (x87 CW & 0x7F) == 0x7F) sse2_path else x87_path`. Dispatchers: `FUN_00B31730` (floor-style, day 7),
  `FUN_00B352E0` (19 callers), `FUN_00B35410` (22 callers), `FUN_00B31840` (`FUN_00401000`'s callee,
  7 203 callers); SSE2 implementations `FUN_00B322DE`, `FUN_00B355DE` (pow-like), `FUN_00B35B5E`
  (log-like), `FUN_00B35DCE` (exp-like), x87 fallbacks through `__trandisp2`/`__cintrindisp2`. The
  SSE2 path is deterministic across CPUs, the x87 one depends on the precision control, and the two
  differ in the last bits. **A patch that sets flush-to-zero or denormals-are-zero in MXCSR (V2DLL's
  "FPU fortress" does) therefore moves every such call to the x87 path**, and a machine with it cannot
  stay in sync with one without it.
- **x87 arithmetic is rare in the simulation.** Of the functions reachable from `DayTick`, 22 use
  extended precision (`float10` in the listing): mostly UI (`EventWindow_ctor`, `FUN_00740050`), the
  int conversion helpers (`FUN_00401000`, `FUN_00401030`), two AI helpers (`FUN_0085FE40`,
  `FUN_0085FFC0`), `FUN_005D0FE0`/`FUN_005D4420`, `FUN_004E9130`. The rest is SSE2 scalar code. So the
  24-vs-53-bit question matters for those few sites, and for nothing else.
- **Who sets the precision.** CRT start: `_controlfp_s(_PC_53)` (`0x00AB231D` region). Direct3D 9
  device creation (`Render_InitRestore`, flags `0x40`/`0x20` only, no `D3DCREATE_FPU_PRESERVE`) lowers
  the creating thread to 24 bits (documented D3D9 behaviour; `oos_log` prints the value seen). Inline
  `fldcw cw | 0xC00` sequences (truncating conversions) save and restore, harmless. No game code reads
  `cpuid` on the simulation path (`FUN_004AAFD0`, `FUN_009E8210` are off it).
- **CRT `rand()`** is `0x00AB047B` (`FUN_00AB047B`, 37 callers; `0x00AAF0C5` with its 329 call sites is an
  `_atol` thunk that was mislabelled `rand` until 2026-10-13). Its callers include `AI_DecisionUpdate`
  (`0x008212FA`, `0x00821453`: the `ai_will_do` roll, see [parallel-triggers.md](parallel-triggers.md)). That
  is a per-frame AI job (`AI_CountryFrameJob`), host-only, so its draws reach clients as commands and do not
  desync. The game never calls `srand` (the CRT init sets `_holdrand = 1` once); the state is per thread,
  which matters for the parallel-triggers work, not for multiplayer. The text generator `FUN_0046B3A0` calls only the `_atol` thunk. The
  simulation's main randomness is the Mersenne Twister instances above.
- **Wall clock on the daily path:** `DayTick` reads `QueryPerformanceCounter` around `FUN_0046ACB0`
  (timing only) and `__time64` only to seed the lazily created event manager; `Province_DailyUpdate`
  and `FUN_00687E30` have the same lazy-creation sequence. Nothing else.
- **V2DLL's pop quantisation** (rounding the int64 fixed-point pop fields to 12 fractional bits after
  the daily pop pass) treats a symptom of the above: the fields are integers, so their low bits can
  only differ through the float conversions feeding them. Keeping the control registers identical
  (`pin_fpu`) addresses the cause; the quantisation would additionally hide a divergence from the
  checksum.

## Lag handling and the chat warning (2026-10-17)

- Every tick each machine sends a `CClientPingCommand` with its date. Its execute (`0x0072F1E0`) on the
  host stores the date in the player's entry (`world+0x18` list, `+0x34`) and compares it with the
  host's: 6-10 days behind lowers the speed (idler `vt[0x168]`) and announces `LAG_DECREASE_SPEED`;
  11 or more pauses and announces `LAG_PAUSE` (both `$PLAYER$ is lagging behind, ...`). A day stamp at
  `0x013F3034`/`0x013F3038` (flags `0x013F303C`) keeps it to once per host day. **[C]**
- The announcement is an ordinary chat line: `ChatMessage_ctor` (`0x009F3290`) builds a 0x74-byte
  message (text +0x08, sender name +0x28, flag country +0x54, key text +0x58), `ChatMessageCommand_ctor`
  (`0x009F4240`) wraps it in the chat command (the one the chat boxes send) addressed to the chat
  channel (`ObjectRegistry_FindByTypeAndId` of the key at `0x00F0D840`), and it is queued with session
  manager+0x38 `vt[0x18]`, which sends it to every player. On execute (`0x009F4530`) the channel stores
  it and notifies its listener (`Chat_OnMessage` `0x00676E80`), which adds a line to the chat list.
  `Chat_ExpireLines` (`0x006784B0`) removes the oldest line once 60 s passed since the last removal,
  one at a time, so repeated warnings linger for minutes. **[C]** Details of the classes:
  [ui-console.md](ui-console.md), "Chat".
- `mp_host_wait_days` (host holds its ticks for a slow player) and `chat_line_seconds` act on this.

## Out-of-sync reaction with the patch (2026-10-17)

- `fix_rng_resync` realigns the draw count to the host's position instead of zero; `state_checksum`
  adds hashes of provinces, countries, pops and the date to the vector; the game compares only its two
  elements and the patch compares the rest (`sync_check.cpp`).
- `oos_notice`: draws differ and the hashes match means the realign recovered the game: `world+0xBA8`
  is set for that comparison (no dialog, no `CPauseGame`; restored at the next comparison) and a chat
  line built exactly like the lag warning, sent by the detecting player, tells everyone. Hashes differ:
  the game reacts as before and the line names the differing hashes. Only the machine that compares
  (a client) can detect it; the host never fails its own check.
- The Force OOS button clears `world+0xBA8` before flipping its element, to get the full reaction on
  every press.

## Open questions

1. What the setup list contains for a long-running game, and whether a joiner really ends up at the
   host's date (test: join a running LAN game, read "REQUESTING SYNCHED" and the next `Checksum:` lines).
2. The save key for token `0x7A4` (the world MT seed): name, and whether the writer emits it.
3. Which `FUN_00697510` branch applies a received state when the client is already in state 4 (needed
   to push a state to an in-game peer without going through the lobby UI).
4. Whether `CPauseGame` as sent on OOS (`+0x58` = 0) pauses or unpauses, and what `+0x59` means.
5. Whether the world's save writer emits the key that seeds the world MT (token `0x7A4`, read by
   `FUN_006817F0`): no other function references the id, and the autosave shows no candidate key, so
   every machine may seed `world+0xB4` from its own clock on load. If so, `FUN_0085CF10`'s daily roll
   differs per machine from day one and `AI_DailyUpdate` runs on different days. To settle by
   logging `world+0xA74` and the first draws on two machines (the `world_rng` element of
   `state_checksum` shows it).
