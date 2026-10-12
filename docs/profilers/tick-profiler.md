# `tick_profiler`

## Source

`patch/src/features/profilers/tick_profiler.cpp`, sites in `tick_profiler_sites.h`. Research:
[../re/systems/tick.md](../re/systems/tick.md).

## Why

To see where a game day goes: how much of it is the daily tick and each of its blocks (provinces,
countries, AI, events, economy), and how much is outside the simulation (render, UI, waiting).

## How

- Call sites of the blocks are redirected to thunks. A thunk notes the time, swaps the return address for a
  stub that notes the end, and jumps to the original with registers and stack untouched, so it works for
  any calling convention. Vtable slots are hooked the same way (the frame, `CInGameIdler_Update`).
- A shadow stack of frames gives inclusive time (block and everything it calls) and exclusive time
  (block minus its hooked children); frames rendered from inside the tick are matched by `ESP`.
- Levels: `1` the main tick blocks and the frame; `2` every game-code call inside `DayTick`; `3` the market
  and country pre-pass; `4` the calls of the frame; `5` internals.
- Every 30 game days a table goes to `the_grand_loader.log`: per block, inclusive and exclusive ms per game
  day and calls per day, plus time outside the simulation and frames per day.
- It hooks some of the same call sites as the optimisations; where both want a site, the first one
  installed keeps it and the other logs that it was skipped.
