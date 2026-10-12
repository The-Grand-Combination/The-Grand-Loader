# Threads

What already runs in parallel, and why the simulation does not.

Sources: research log 2026-10-01 (TBB); `Economy/economy-market.md` (worker framework, accumulators) in
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering).

## Summary

- The engine links Intel TBB. `task_scheduler_init::initialize(threads)` only matters for three
  `parallel_for` sites: entity/spatial registration near `Entity_RegisterSpatial` (`0x0068F390`) and
  graphics near `Render_InitRestore` (`0x0099DA20`). The daily simulation is single-threaded. The
  shipped option `engine_worker_threads` hooks that import (IAT) and sets the thread count.
- There is also a small generic worker-thread class (vtable `0x00E439F8`): `WorkerThread_Create`
  (`0x00A7AED0`) calls `CreateThread` with `WorkerThread_Entry` (`0x00A7B0C0`), which dispatches to
  `[this+0x18]->vt[1]` (`WorkerThread_Dispatch` `0x00A7B090`). Which job objects use it is unknown.
- The economy writes into shared 64-bit accumulators (`Economy_CalculateDistribution` writes
  `this+0x8D8+i*8`, `this+0x900+i*8`, `this+0x13E8/+0x13EC`; `Economy_AccumulateElement` `0x00969760`
  does `+=` on one object). Running iterations of those loops on several threads would race; a
  parallel version needs per-thread accumulators merged at the end.

## Open questions

1. Who instantiates the worker-thread class, and what its job does.
2. Whether `WorldMarketUpdate`'s per-country loop (the largest daily block) has the same shared-state
   problem, or is per-country clean.
