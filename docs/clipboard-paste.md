# `clipboard_paste`

## Source

`patch/src/features/interface/clipboard_paste.cpp`.

## Why

The game's text fields take characters from the `WM_CHAR` messages of its window. Pressing Ctrl+V
produces a `WM_CHAR` with code 0x16, which the edit boxes drop, so an IP address or a name cannot be
pasted into the multiplayer dialogs or anywhere else.

## How

- The game's import of `DispatchMessageA` goes to a wrapper. Every message passes through unchanged
  except the `WM_CHAR` of Ctrl+V, which is swallowed while the clipboard text is kept. Once Ctrl is
  released, the text is posted to the same window as one `WM_CHAR` per character, and the game handles
  them as if they were typed. Sending them at once did not work: while Ctrl is held the game does not
  pass characters to the text field.
- The first five pastes are logged with the number of characters found on the clipboard.
- The text is the clipboard's ANSI text, printable characters only (line breaks and tabs dropped),
  with leading and trailing spaces trimmed, at most 256 characters.
