# `engine_worker_threads`

**Default 4** (`0` = the game's own choice). Sets the thread count of the game's own TBB scheduler.
Source: `patch/src/features/performance/worker_threads.cpp`. Research: [re/systems/threads.md](re/systems/threads.md).

## Why

The game links Intel TBB and initialises it with `task_scheduler_init::initialize(-1)` (TBB's default).
Only a few daily loops use it, the largest being the pop statistics `parallel_for` called from `DayTick`
(`FUN_0068F4C0`, ~4 ms/day on the game thread in 1836). The rest of the simulation is single-threaded.

## How

The patch redirects that import (IAT) and passes the configured count instead (1–64).

## Result

Not measured on its own. With 24 logical processors the game asked for −1 and got the TBB default; values
above 4 have not been compared. It is independent of the worker pool used by `parallel_decisions` and
`parallel_events`, which runs at other moments of the day.
