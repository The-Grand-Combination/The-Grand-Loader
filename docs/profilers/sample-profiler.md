# `sample_profiler`

## Source

`patch/src/features/profilers/sample_profiler.cpp`; reader `docs/tools/scripts/annotate_samples.sh`.

## Why

`tick_profiler` only times calls it can redirect; it cannot see `CALL EAX` / `CALL [reg+n]`, other DLLs or
the instruction the game is waiting on. A sampling profiler needs no hooks: it looks at the game thread at
regular intervals and counts where it is.

## How

- From the first game tick, a thread wakes every `sample_profiler` ms while the date advances (pauses and
  menus are skipped), suspends the game thread, reads `EIP`/`ESP`, copies 16 KB of its stack and resumes it.
- The stack copy is walked along the `EBP` chain, and the gaps are filled with return addresses that follow
  a `CALL` whose target contains the younger frame (this drops most stale addresses left in locals).
- Each stack is added, leaf first, to a collapsed-stack table. Exe addresses are written as listing VAs
  (base `0x00400000`; the exe itself is relocated by ASLR), leaves in other DLLs as their nearest export,
  and leaves in `performance_patch.dll` as `cp+0x<RVA>`.
- Every 30 game days the table is rewritten to `performance_patch_samples.tsv` next to `v2game.exe`.
- `annotate_samples.sh <file> [function]` maps it to functions through `docs/re/decomp/index.tsv` and the
  DLL's link map (`out/x86-release/bin/performance_patch.map`, or `CP_MAP=<map>`): time by module,
  exclusive and inclusive per function, other DLLs by the exe function that called them, and the callers
  and callees of one function.
- Run it with `tick_profiler = 0`, or the profiler's own hooks show up in the samples.
