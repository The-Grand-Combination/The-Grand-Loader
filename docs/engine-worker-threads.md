# `engine_worker_threads`

## Source

`patch/src/features/performance/worker_threads.cpp`. Research: [re/systems/threads.md](re/systems/threads.md).

## Why

The game links Intel TBB and initialises it with `task_scheduler_init::initialize(-1)` (TBB's default).
A few daily loops use it, the largest being the pop statistics `parallel_for` called from `DayTick`
(`FUN_0068F4C0`); the rest of the simulation is single-threaded.

## How

The patch redirects that import (IAT) and passes the configured thread count instead (1–64; `0` keeps the
game's choice). It is independent of the worker pool of `parallel_decisions` and `parallel_events`, which
runs at other moments of the day.
