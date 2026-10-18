# `save_last_ip`

## Source

`patch/src/features/interface/last_ip.cpp`.

## Why

The multiplayer join dialog ("Enter IP address...") already starts with the `lasthost` value of
`settings.txt` (one per mod, in the mod's folder under Documents), and the game stores the typed
address there when you press Join. It writes the file itself only once the connection succeeds and
the lobby opens, and otherwise from a few menus or on a clean exit, which a normal close does not
reach. An address that did not connect, the usual case while setting a game up, is lost.

## How

- The call that stores the address in the join dialog's OK handler (`0x00767328`) goes to a thunk
  that performs it and then calls the game's own settings save, the same routine its menus use.
- Nothing else changes: the dialog keeps reading `lasthost` as before.
