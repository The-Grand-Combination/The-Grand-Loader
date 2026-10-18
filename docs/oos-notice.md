# `oos_notice`

## Source

`patch/src/features/multiplayer/oos_notice.cpp`, called from `sync_check.cpp`. Requires
`state_checksum`.

## Why

When the daily sync check fails, the game shows a dialog and pauses everyone. With `fix_rng_resync`
the random generator is realigned to the host's position, and if the game state still matches, the
game simply carries on in sync: the dialog and the pause interrupt for nothing. When the state really
differs, the game keeps playing out of sync, and the dialog says nothing about what differs.

## How

- Before the game compares its two elements (draw count and seed), the patch compares the state hashes
  `state_checksum` adds (provinces, countries, pops, date) with the host's.
- Draws differ and the state matches: the game's dialog and pause are skipped for that comparison, and a
  chat line says the game was out of sync and recovered.
- Draws differ and the state differs: the game reacts as usual, and the chat line names what differs and
  suggests saving and reloading.
- Draws match but the state differs (nothing in the game reacts): a chat line says so, once until the
  state matches again.
- The line is built and sent exactly like the host's "X is lagging behind" warning: a chat message with
  the local player as sender and their flag, wrapped in the game's chat command and queued to the
  session, so it appears in the same chat list on every player's screen and leaves with
  `chat_line_seconds`.
- The game's "dialog already shown" flag is restored at the next comparison, so a later divergence
  still gets the game's reaction.
