# `chat_line_seconds`

## Source

`patch/src/features/interface/chat_lines.cpp`.

## Why

In multiplayer, when a player falls six days behind, the host lowers the speed and sends every player
the chat line "X is lagging behind, decreasing speed." It repeats while the player stays behind. The
in-game chat list drops its oldest line only after a minute has passed since the last removal, one
line at a time, so a few repeated warnings stay on screen for minutes.

## How

- Chat lines carry no time of their own: the game counts from its last removal. The patch records when
  each line appeared, from the line count before the per-frame routine that trims the list runs.
- When the oldest line is older than the configured value, the patch makes that routine remove it on
  the same frame. Every line, the players' messages included, stays the configured time, also when
  several arrive together. 60 or 0 leaves the game as it is.
- The game's own one-minute rule is raised to 127 seconds, so it never removes a line early.
