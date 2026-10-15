# `oos_test_button`

## Source

`patch/src/features/multiplayer/oos_test.cpp`.

## Why

To test what happens on an out-of-sync (the game's dialog and pause, `oos_log`, a future resync)
without waiting for a real one.

## How

- A small always-on-top tool window with a "Force OOS" button, owned by a thread of the patch, is kept
  over the top-right corner of the game window (repositioned four times a second; hidden when the game
  is minimised). It never takes focus. Exclusive fullscreen hides it; Ctrl+Shift+O, polled on the game
  thread at every tick while the game window has the focus, works everywhere.
- On the next hourly tick the first element of the local sync vector (`world+0xB74`, the random draw
  count) is flipped before the game compares it with the host's. The comparison fails and the game
  takes its real out-of-sync path. The tick command was copied from the vector before the flip, so
  only the machine that pressed it fails its own comparison, host or client; the others still match
  the command. With `fix_rng_resync` the machine is back in sync one day later.
- The game shows the dialog and sends the pause only once per session (a flag at `world+0xBA8` that
  it never clears). The button clears it before the flip, so every press gets the full reaction.
- One line goes to `the_grand_loader.log` with the day and the new value.
