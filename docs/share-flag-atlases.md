# `share_flag_atlases`

## Source

`patch/src/features/memory/flag_atlas_share.cpp`. Research: [re/systems/flags.md](re/systems/flags.md).

## Why

The game builds one flag atlas per government, not per flag type. Governments with the same `flagType`
(`absolute_monarchy` and `prussian_constitutionalism` are both `monarchy` in vanilla) each load their own
copy of the same `flagfiles_<flagType>` texture and keep it for the whole session. A mod with 18
governments and 11 flag types holds 18 atlases of 4096×2048:
- 7 duplicates of 4 MB each with `compress_flag_cache` (28 MB);
- 32 MB each without it (224 MB), in process memory and in video memory.

## How

The call that builds or loads each government's atlas (`0x007E0DE1`) goes through a wrapper:
- The first government of each flag type builds as before.
- A later one gets a copy of that atlas: the same texture and the same cell tables.

The game never frees an atlas alone, so sharing is safe. The flags drawn are the same pixels: display
only, nothing else changes. The log has one line per shared atlas at start.
