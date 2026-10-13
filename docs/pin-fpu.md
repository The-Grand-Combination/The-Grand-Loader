# `pin_fpu`, `fpu_precision`

## Source

`patch/src/features/multiplayer/fpu_pin.cpp`; reverse engineering in
[re/systems/multiplayer.md](re/systems/multiplayer.md) ("Floating point and the C runtime").

## Why

Two players only stay in sync if every floating-point operation gives the same bits on both machines.
Three things decide that outside the game's own code: the x87 precision control (Direct3D 9 lowers it
to 24 bits on the thread that creates the device, and the game does not ask it not to; a game under a
translation layer such as dxvk keeps 53), the rounding mode, and MXCSR, which the statically linked C
runtime inspects on every `pow`, `exp`, `log` or `floor` call to pick between its SSE2 and its x87
implementation. Anything that changes one of them on one machine only (another patch that enables
flush-to-zero, a driver or overlay, a different platform) makes the two simulations drift.

## How

- On every hourly tick, before the day is simulated, the x87 control word is rewritten: precision from
  `fpu_precision` (24 or 53 bits), round to nearest, all exceptions masked; and MXCSR is set to its
  default (`0x1F80`: round to nearest, no flush-to-zero, no denormals-are-zero). Pending x87 exceptions
  are cleared first.
- The first tick logs what it found and what it set, so a player can see which precision the game had
  on their machine. `oos_log` reports the same value at the first sync check.
- `fpu_precision = 24` reproduces what vanilla gets on Windows; `53` is what the C runtime starts with.
  Every player must use the same value.
- The patch's own worker threads already copy both registers from the game thread before each batch.
