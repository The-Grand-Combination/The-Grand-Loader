# `fix_tech_window_crash`

## Source

`patch/src/features/stability/tech_window_guard.cpp`.

## Why

The technology window lists inventions. Two of its routines reach each invention's status object
through three unchecked pointers, invention `+0x430`, then `+0x310`, then `+0x40`: the sort
comparator used by the list (`FUN_007A9070`, mode 0, compares the field at the end of the chain)
and the folder-icon builder (`FUN_007ADB70`, passes the name at the end of the chain to a string).
An invention whose status object is missing, which mods can produce, crashes the window on either
path. V2DLL reported both crashes on 3.04.

## How

- In the comparator, the two loads at `0x007A918A` are replaced by a jump to a guard that tests all
  four pointers. With all present it replays the loads and continues. With one missing it returns
  "not less", so the two inventions compare equal, which keeps the sort's ordering valid.
- In the folder builder, the three loads at `0x007ADE98` are replaced the same way; with a link
  missing the guard passes an empty name instead of the folder name.
- Nothing else in the window changes; an invention with a complete status object is handled by the
  original instructions.
