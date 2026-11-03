# `fix_edge_scroll`

## Source

`patch/src/features/stability/window_mode.cpp`.

## Why

The map scrolls when the cursor is within a few pixels of the view's edge. The game measures the
cursor relative to its window but never checks that it is inside it, so a cursor on a second monitor,
or anywhere outside a window smaller than the screen, scrolls the map without end. Players work around
it with tools that lock the cursor in the window.

In windowed mode the game also measures the cursor from the window's outer corner, minus a frame
thinner than the one Windows 10/11 draws. The edge bands of the top and left edges then lie on the
title bar and border, and the bottom and right ones start inside the view.

The edge band is only a few pixels wide; in a window, the cursor easily overshoots it.

## How

- The per-frame camera update of an active game window gets edge scrolling switched off for that
  frame unless the game is the foreground window and the cursor is over its client area.
- The mouse position the game reads is taken from the client area, so the edge bands, clicks and
  tooltips line up with what is drawn.
- `edge_scroll_lock` (with `fix_edge_scroll`): while the map is shown and the game has focus, the
  cursor is held inside the game view, so it stops on the edge band. Pushing the mouse further out than
  the value (in mouse counts, measured with raw input) releases it just outside that edge; it is held
  again once it comes back in. Alt-tab or any loss of focus releases it. 0 turns the hold off.
- Arrow keys, middle-button dragging and everything else in the camera update are unchanged.
