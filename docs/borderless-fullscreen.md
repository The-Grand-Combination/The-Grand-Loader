# `borderless_fullscreen`

## Source

`patch/src/features/stability/window_mode.cpp`.

## Why

With `fullScreen=yes` the game runs in exclusive fullscreen. Every alt-tab loses the Direct3D device:
the window is minimised, the game sleeps a second, releases and resets every graphics resource when it
comes back, and runs at ten frames a second meanwhile. A reset that fails leaves a black screen, and
alt-tab is a common source of hangs. The game also has a borderless mode (`fullScreen=no`,
`borderless=yes` in `settings.txt`): a popup window with a windowed device, which alt-tab does not
disturb. It uses the resolution from `settings.txt`, so it only covers the screen when that matches
the monitor.

## How

- Right before the game creates its window, the graphics settings are set to borderless at the size
  of the primary monitor. The window, the Direct3D device and the mouse all read those settings, so
  the game runs in its own borderless mode.
- The game may write those values back to `settings.txt`.
- `0` leaves the window to `settings.txt`.
