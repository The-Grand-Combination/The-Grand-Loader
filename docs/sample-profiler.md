# `sample_profiler`: where the game thread is

Diagnostic feature of `performance_patch.dll`. Source: `patch/src/features/profilers/sample_profiler.cpp`.

## Why

`tick_profiler` times functions by redirecting `CALL rel32` sites and vtable slots. It cannot see
`CALL EAX` / `CALL [reg+n]`, and at max speed the largest block it found, `CommandQueue_Process`
(~51 ms/day of its own), spends its time in exactly such calls. A sampling profiler needs no hooks:
it looks at the game thread at regular intervals and counts where it is.

## How it works

On the first game tick it starts a thread that, every `sample_profiler` ms while the date is advancing
(pauses and menus are skipped):

1. suspends the game thread, reads `EIP`/`ESP`, copies up to 4 KB of its stack, and resumes it. Nothing
   else runs while it is suspended (no allocation, no lock), so it cannot deadlock;
2. walks the copy for return addresses into the exe. A `CALL rel32` counts only when its target
   contains the younger frame's address (or is a `JMP [import]` thunk when the leaf is in another
   module), which drops stale return addresses left in local variables; indirect calls cannot be checked
   and are accepted;
3. adds the whole stack (leaf first; a leaf outside the exe is its module: `ntdll.dll`, `d3d9.dll`, the
   driver, ...) to a collapsed-stack table.

Addresses are written as listing VAs (base `0x00400000`). The exe is relocated by ASLR at runtime (the
first run had it at `0x00080000`), and raw runtime addresses would not match `docs/re/decomp/`. Each sample is
weighted by the real time since the previous one. Totals are cumulative from the first game day (reset on
a load) and rewritten to `performance_patch_samples.tsv` next to `v2game.exe` every 30 game days.

Format: `mod <id> <dll>` lines, then `stack <samples> <ms/day> <leaf;caller;caller;...>`.

Cost: one suspend/resume per ms, a few µs each, about 1 % of the game thread. Run it with
`tick_profiler = 0`: the profiler's hooks would show up in the samples.

## Reading it

Leaves inside `performance_patch.dll` are written as `cp+0x<RVA>` and named from the link map
(`out/x86-release/bin/performance_patch.map`, or `CP_MAP=<map>` for the map of the DLL that took the
samples). Every other DLL is named by its nearest export.

```sh
docs/tools/scripts/annotate_samples.sh "<game dir>/performance_patch_samples.tsv"                        # modules, top exclusive / inclusive
docs/tools/scripts/annotate_samples.sh "<game dir>/performance_patch_samples.tsv" CommandQueue_Process   # its call sites by time
```

The script needs `docs/re/decomp/index.tsv` (see [docs/re/ghidra.md](re/ghidra.md)). It prints time by
module, exclusive and inclusive time per function (inclusive counts a function once per stack, so
recursion is not doubled), time in other DLLs by the exe function that called out, and for a zoomed
function its callees and callers.

- **Exclusive** is exact up to sampling noise (~100 samples per game day at max speed).
- **Inclusive** depends on the stack walk. Calls through `CALL EAX` / `CALL [reg+n]` cannot be checked,
  so a stale return address after an indirect call can still slip in. Trust large differences.
