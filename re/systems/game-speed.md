# Game speed

How the speed setting (1–5 in the UI) turns into game days per second.

Sources: `Engine/game-speed.md` in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).
The table values were re-read from the 3.04 exe in this repo (`tools/scripts/check_patch_sites.sh`
reads the same file offsets).

## Summary

`world+0xB28` is the speed index 0..4, changed by `GameSpeed_Increase` (`0x0072EE90`) /
`GameSpeed_Decrease` (`0x0072EFE0`) and clamped to 0..4. Two independent tables are indexed by it:

| index | UI | `kSpeedThrottle` (`0x00F0956C`, used by `UpdateTimeThrottle`) | `ProcessGameTimeAdvance` threshold |
|---|---|---|---|
| 0 | slowest | 0.03 | 4.0 (`0x00F17B58`) |
| 1 | slow | 0.03 | 2.0 (`0x00F17B54`) |
| 2 | normal | 0.03 | 1.0 (`0x00F092FC`, shared constant) |
| 3 | fast | 0.04 | 0.5 (`0x00F17898`, shared constant) |
| 4 | fastest | 0.06 | 0.0004 (`0x00E45BB8`, shared constant) |

All values **[C]** (read from the exe). The UI names are the strings at `0x00E11040..0x00E11078`.

## `ProcessGameTimeAdvance` (0x00682BD0)

Called from `CInGameIdler_Update` every frame. **[C]** unless marked

1. `world+0xB14 += frame time`.
2. Returns when `world+0xB20 != 0` (paused / blocked).
3. Builds the 5-entry threshold table on the stack from the constants above and compares
   `world+0xB14` with `table[speed]`.
4. A second gate at `0x00682D05`: `CMP byte [world+0xBB8], 0; JZ` skips the temporal processing.
   Removing the `JZ` (patch site `time_barrier`) changes behaviour, so it is a real gate; its meaning
   is unknown.
5. Updates `g_timeFactor` (`0x012588F0`: ×0.95, ×0.9, +0.5 depending on how far behind the date is)
   and `0x012588E6` / `0x012588EC`. **[I]**

At speed 4 the threshold is 0.0004 s, so the day advances on practically every frame: max speed is
frame-bound, which is why render and UI cost (~90 ms/day, [tick.md](tick.md)) dominate it.

## `UpdateTimeThrottle` (0x00685620)

Called from inside the daily tick (1375 times per day). It computes
`kSpeedThrottle[speed] × g_throttleScale (0x013F2AE8) × (g_timeFactor + 1.0)` and, when that much
real time has passed, runs a whole frame through `vtable+0x100` → `App_ProcessMessagePumpAndUpdate`
(`0x009DF2B0`). This keeps the window responsive during long ticks. One call inside the world market
(`0x00484309`) was removed in an external patch (`world_market_throttle`); the side effect is that a
long market update no longer pumps messages.

## Notes for patches

- `0x00F092FC`, `0x00F17898` and `0x00E45BB8` are shared constants with other readers
  (`0x00F092FC` is also read by `FUN_00475150`). Writing a new value there changes those readers too;
  a speed patch should redirect the load in `ProcessGameTimeAdvance` (`0x00682C1F..0x00682C59`) to its
  own constant instead.
- Earlier experiments (a 4.0f code cave, 5.0 at `0x00F092FC`, 0.000001 at `0x00E45BB8`, NOPs at
  `0x00682D0C`) were not kept; one of them reused a code cave already used by another patch and
  crashed.

## Open questions

1. Meaning of `world+0xB20` and `world+0xBB8`.
2. What `g_throttleScale` holds and who writes it.
