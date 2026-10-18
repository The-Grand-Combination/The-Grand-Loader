# `chat_line_seconds`

## Source

`patch/src/features/interface/chat_lines.cpp`.

## Why

In multiplayer, when a player falls six days behind, the host lowers the speed and sends every player
the chat line "X is lagging behind, decreasing speed." It repeats while the player stays behind. The
in-game chat list drops its oldest line only after a minute has passed since the last removal, one
line at a time, so a few repeated warnings stay on screen for minutes.

## How

- The per-frame routine that trims the chat list compares the seconds since its last removal with 60.
  That constant is replaced by the configured value (at most 127; 60 or 0 leaves the game as it is).
- It applies to every chat line, the players' messages included. Lines still leave one at a time, so
  three queued lines take about three times the value.
