# Loading a game

Sources: `Engine/loading-initialization.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering);
callers checked here.

## Summary

`Game_FinalizeLoadAndEnterGameplay` (`0x00662010`, `CInGameIdler` vtable slot `0x00E0A358`) runs once
after a save or new game is loaded and before play. In order: **[I]** unless marked

1. Load phase `[this+0x1E08] = 3`; screen size into `+0x1E10/+0x1E14/+0x1E18`.
2. Input/event bindings by string key.
3. Resets a runtime field of every entity (`+0x0C = 0`).
4. Subsystem init chain: `FUN_00521560`, `0050E540`, `005068F0`, `00521FD0`, `0050EA30`, `00530E40`,
   `0052FCD0`, `0051C890`, `005143B0`, `00517AF0`, `0051EB40`.
5. Entity id repair: a zero id at `+0x34` gets a new one, capped at 100000.
6. Caches config objects looked up by string into `+0x11C8`, `+0x1268`, `+0x12B8`, `+0x1308`, ...
7. Unless `world+0xD11`: registers every entity in the spatial grid (`entity+0x1D0 = this`,
   `FUN_00407B10`, `Entity_RegisterSpatial`).
8. Logs the load time; full economy rebuild `EconomyManager_UpdateFull` (call at `0x0066321D`)
   **[C]**; decision list setup `Decisions_Setup` (`0x00505010`).
9. State `0xB` (`FUN_006480A0`, `FUN_00648040(this, 0xB, -1)`), then 32 subsystem callbacks
   (`[this+0xD80+i]->vt[0x24/4]`), like an "on game started" broadcast.

Features that cache per-game state (both culls) detect a load by a date jump instead of hooking this
function.
