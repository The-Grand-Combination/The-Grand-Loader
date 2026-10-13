# `sync_world_rng`

## Source

`patch/src/features/multiplayer/world_rng.cpp`; reverse engineering in
[re/systems/multiplayer.md](re/systems/multiplayer.md), "Random number generators".

## Why

Besides the global generator that the daily check watches, the world object carries its own
MT19937, seeded from the clock when the world is created and never written to the save. Two machines
therefore never share it, and the `world_rng` element of the state checksum differed on day 2 of the
first two-instance test. It is read by code that every machine runs in lockstep: the daily roll that
decides whether an AI country thinks today, and the roll by which an AI country accepts or refuses a
diplomatic action when the command executes. It is also read by code that runs on one machine only:
the AI's per-frame jobs on the host, and the tooltip of a diplomatic action, which runs the very same
acceptance roll locally. So in vanilla the AI's diplomatic answers drift apart between players from
the first day, and hovering a tooltip moves one machine's generator further.

## How

- The ten call sites that draw from the world generator are redirected to one dispatcher. While the
  session manager is executing a command (its "executing" flag covers every command, the hourly tick
  with the whole `DayTick` included) all machines run the same code in the same order, and the draw is
  taken from the global generator and counted like the game's own draws, so the daily check covers it.
  Outside command execution (AI frame jobs, tooltips) the draw comes from a private generator seeded
  from the clock, which no longer matters to anyone else.
- The world generator itself is left alone; nothing reads it any more.
- Single-player results change: the AI's daily timing and diplomatic answers now come from the
  global sequence. Every player of a multiplayer game must use the same value.
