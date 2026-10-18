# `fix_edge_scroll`

## Source

`patch/src/features/stability/window_mode.cpp`.

## Why

The map scrolls when the cursor is within a few pixels of the view's edge. The game measures the
cursor relative to its window but never checks that it is inside it, so a cursor on a second monitor,
or anywhere outside a window smaller than the screen, scrolls the map without end. Players work around
it with tools that lock the cursor in the window.

## How

- The per-frame camera update of an active game window gets edge scrolling switched off for that
  frame unless the game is the foreground window and the cursor is over its client area.
- Arrow keys, middle-button dragging and everything else in the camera update are unchanged.
