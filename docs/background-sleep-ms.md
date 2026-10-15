# `background_sleep_ms`

## Source

`patch/src/features/performance/background_sleep.cpp`.

## Why

Both frame loops of the game (`App_MainLoop`, `App_ProcessMessagePumpAndUpdate`) start a frame
with a sleep whose length depends on one flag of the render device: 0 ms while the device is
available, 100 ms while it is not. The flag is cleared when Direct3D reports the device lost, which
happens to a fullscreen game when another window takes the screen, and set again when the device is
reset. So a fullscreen game in the background runs at ten frames a second. In single player that
only slows a game left running; in multiplayer a client that alt-tabs out processes the host's
ticks at that rate and falls behind, which is what the "lagging behind" handling then reacts to.

A windowed game is not affected: its device is not lost on focus changes.

## How

- The two `PUSH 100` immediates in front of the `Sleep` calls (`0x009DF684`, `0x009DF2D5`) are set to
  the configured value. 1 ms is the default; the game raises the timer resolution to 1 ms itself
  when it initialises its network layer, so the sleep is as short as asked for. 100 leaves the
  game as it is.
- Nothing else changes: the 0 ms branch for an available device is untouched.
