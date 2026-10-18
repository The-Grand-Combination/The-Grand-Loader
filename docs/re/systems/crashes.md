# Crashes: causes and guards

Crashes found while playing modded games with the patch, what in the game causes them, and the guard
that now prevents each one. Every guard checks the original bytes before patching and changes nothing
for a game that would not have crashed. Sources: this repo's sessions (2026-10-15 to 2026-10-17), from
user logs, crash dumps and the decompilation. `[C]` confirmed in the listing, `[I]` inferred.

## How a crash is diagnosed

- `the_grand_loader.log` (or `the_grand_loader.<pid>.log` for a second instance) has the crash logger's
  lines: exception, faulting address as a listing VA, thread, registers, exe addresses found on the stack.
- **Heap corruption (`0xC0000374`) bypasses the logger**: ntdll reports it through
  `RtlReportFatalFailure`, not through the vectored handler, so the log just stops. Windows Error
  Reporting keeps a record (event 1001, `APPCRASH`) and a minidump in `%LOCALAPPDATA%\CrashDumps`; the x86
  `cdb.exe` of the WinDbg package reads it (`.ecxr; kv`).
- The game's own logs are per mod (`Documents\Paradox Interactive\Victoria II\<mod>\logs`) and shared by
  every instance running that mod.

## Vanilla crashes

| crash | cause | guard |
|---|---|---|
| Event window with an `add/remove_*_modifier` whose name does not exist | the parsers (`0x00881260`, `0x008820D0`) store a null modifier and log "Unknown modifier-type"; the executes are null-safe, the tooltip builders (`0x008814E0`, `0x00881B40`, `0x008822C0`, `0x008826B0`) call `modifier->vt[0x18]` unchecked **[C]** | `fix_missing_modifier`: empty tooltip, skipped execute, unknown names logged; names defined in a file read later (`issues.txt` is parsed before `event_modifiers.txt`) are looked up again on first use |
| AI daily scan with an army in a province without an owner (mods that uncolonise provinces) | `province+0xE8` (state) is null; `AI_UnitTargetScan` (`0x00851BD0`) calls `State_HasHostilePresence` (`0x0048F070`) / `State_GetControllerTag` (`0x0048E120`) and walks `state+0x14` at `0x00851F51` without a test; the scoring at `0x00853BEF` likewise **[C]** | `fix_missing_state_crash`: "no state" answers, not-found branch |
| Production view of a country with a factory whose type has no local-supply source | `FUN_004D1560` reads `type+0x12C` (null) at `0x004D15EB` **[C]** (reported by V2DLL) | `fix_supply_crash` |
| Technology window with an invention without a status object | `invention+0x430 -> +0x310 -> +0x40` unchecked in the sort comparator (`0x007A918A`) and the folder builder (`0x007ADE98`) **[C]** (reported by V2DLL) | `fix_tech_window_crash` |
| Pie chart with a slice of more points than the vertex buffer | `FUN_009E0EF0` writes the slice's points into a 153-entry stack buffer unchecked **[C]**; needs corrupt chart data | none (implemented, then dropped: never seen) |

## Crashes caused by the patch

| crash | cause | fix |
|---|---|---|
| Heap corruption on a `parallel_events` worker (TGC) | `is_possible_vassal` and `can_create_vassals` rebuild a vector stored in the country (`Country_RebuildPossibleVassals` `0x00519A40`, `country+0xE2C`) on every call; two workers on the same country resize it at once **[C]** (dump) | `parallel/trigger_locks.cpp`: both Evaluate slots serialised with a critical section; all 236 trigger classes audited for other writers ([parallel-triggers.md](parallel-triggers.md)) |

## Open

- The audit for writing triggers follows direct calls three levels deep and no virtual calls; a writer
  reached only through a virtual call would not have been found.
- An army on ownerless land is a state the engine was not written for: the guards cover the AI scan
  that crashed; battles, supply or movement on such provinces have not been checked.
