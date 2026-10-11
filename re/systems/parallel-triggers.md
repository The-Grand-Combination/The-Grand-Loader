# Parallel trigger evaluation: feasibility

Can the AI's daily decision checks run on worker threads? Option 4 of [../next-steps.md](../next-steps.md).
Study of 2026-10-11; nothing built yet.

Sources: this repo's sessions; static scan [raw/trigger-writescan-2026-10-11.txt](../raw/trigger-writescan-2026-10-11.txt)
(`tools/ghidra/V2WriteScan.java`); samples and decision profile of the 1836 runs.

## Summary

- **Semantics allow it.** The AI's decision loop does not change the game while it runs: a chosen
  decision becomes a command for the session queue and runs later in `CommandQueue_Process`. **[C]**
- **The triggers look read-only.** From all 235 `Evaluate` functions, every write to a global found in
  direct calls (depth 8, 688 functions) is a lazy singleton (`if (g == 0) g = new ...`), a write of the
  value already there, CRT internals or crash paths. **[C]** Virtual calls into non-trigger objects
  were not followed, so this is not a proof.
- **The prize in 1836 is small:** ~5–6 ms/day of `potential`/`allow` remain after `cull_decision_checks`,
  so ~4 ms on 4 threads. It grows with the late game, which is not profiled yet.
- **Proposal:** a shadow mode first (compute in parallel, still evaluate serially, log mismatches),
  then the real switch only if years of play show zero mismatches.

## The loop (`AI_DecisionUpdate`, `0x00821190`) **[C]**

Once per AI country per day, from the AI job queue (`AI_CountryFrameJob` -> `0x0081EA9B`). For each
decision of `g_decisionManager` (two lists, `+0x28` and `+0x0C`), in order:

1. `CDecision` vtable slot 6 (`0x00A31490`), called twice;
2. `potential` (`CAndTrigger` at decision `+0x24`) and `allow` (`+0x40`), each `Evaluate(scope)`;
3. `CDecision_EvaluateAiWillDo` -> chance; `CRT rand() % 100` (`0x00AB047B`) against chance × 100;
4. on success: build a command (`FUN_00905B30`) and hand it to the session (`world+0xB24` vt `+0x44`,
   then `+0x18`); stop.

The CRT `rand` is not the simulation's Mersenne Twister, and the MSVC multithreaded CRT keeps its state
per thread. Only steps 1–2 are candidates; step 3 must stay serial and in order so the same rolls
happen.

## Thread safety of `Evaluate` (static scan)

`V2WriteScan` follows direct calls from each root and lists writes to absolute addresses and calls to
the RNGs. 63 addresses, by kind:

| kind | addresses | verdict |
|---|---|---|
| lazy singletons: `CNullPop` (`0x01258A10`), `g_cbManager`, `g_popTypeDb`, `0x012587EC`, `0x0125880C`, `0x01258A2C`, `0x01258A34`, `0x01258728`, `0x01258730`, `0x012586EC` | 12 | created at load; no race in mid-game. The pop constructor (`0x00954A40`) behind `CNullPop` is also where the only **MT draw** (`Rng_Next`) and most other writes come from |
| "store the same value" helpers (`0x0125CE80`, `0x0125EA24`, `0x0131C99C`: `if (p == g) g = p`) | 3 | harmless |
| CRT: `operator new` handler, `atexit`, `NMSG`, `snprintf` buffer, `report_gsfailure` | ~40 | error paths, or internally locked |
| `ExceptionList` (`fs:0`) | 1 | per thread |

Not covered, to check before building:

- **virtual calls** to non-trigger objects (the flag set of `has_country_flag` at `country+0x1B0`
  vt `+8`, modifiers, country/province virtuals);
- **writes through pointers** (a lazily computed value cached in a country or province object);
- **heap traffic**: allocations inside triggers take the CRT heap lock, which would serialise the workers.

## Requirements for a worker-thread version

- **FPU state.** Worker threads must run with the game thread's x87 control word and MXCSR (Direct3D 9
  may have lowered the x87 precision). Otherwise float comparisons in triggers can differ.
- **Frozen inputs.** Precompute when the AI queue is filled (end of `DayTick`) or at the first
  `AI_FrameUpdate` of the day, and invalidate when anything runs that changes the game: a command
  executed by `CommandQueue_Process`, or the rest of an AI job (`AI_CountryDailyJob_Update`, `0x0084B690`)
  if it writes directly. With `fast_ai_queue` the queue drains in one frame, so one precompute covers
  the day.
- **Only steps 1–2 in parallel**, results as one bit per (country, decision); the original loop reads
  the bit instead of calling `Evaluate`, and does `ai_will_do`, `rand` and the command as before.
- Threads: our own small pool (simplest to control, sets the FPU state) rather than the game's TBB.

## Plan

1. **Shadow mode** (`parallel_decisions = 2`, diagnostic): workers precompute the bits; the original loop
   still evaluates every `potential`/`allow` and compares; mismatches go to `v2cp.log` with country,
   decision and day. Measures the parallel time too.
2. Run several game years (early and late saves). Any mismatch: find the input that changed or the
   hidden write.
3. **Real mode** (`parallel_decisions = 1`) only with zero mismatches.

Same pattern, later, for other read-only scans: the AI target scans (`AI_TargetScan`, `0x0084B0F0`) and
the event `trigger`/MTTH checks.

## Implementation: `parallel_decisions` (2026-10-11; shadow run: 0 mismatches in 162 M checks)

- **Pool** (`patch/src/parallel/worker_pool.cpp`): logical processors − 1 workers (max 7), started on
  first use; each batch copies the caller's x87 control word and MXCSR to the workers; the game thread
  takes part in the batch.
- **When:** `fast_ai_queue` calls `decisions_frame_begin` before draining the queue at max speed and
  `decisions_frame_end` after. Results are only used inside that frame. The `fast_ai_queue` thunk now
  saves the full FPU/SSE state (`fxsave`/`fxrstor`).
- **Who:** the countries in bucket 0 of the queue (node `+4` = country index, next at `+0xC`) that
  have an AI object (`country+0x208`). Their cull lists are rebuilt first on the game thread if due.
- **What:** per country, a worker builds the AI scope (`CEventScope::ctor(scope, 0x2D)`, `{tag, index}`
  from AI object `+0x30`) and evaluates `potential` (`+0x24`) and `allow` (`+0x40`) of every decision
  of the cull lists, in order; slot 6 is checked to still be `MOV AL,1 / RET` (`0x00A31490`). Output: the
  passing decisions per list, in vectors reserved beforehand (no allocation on workers).
- **Use (`= 1`):** `AI_DecisionUpdate`'s fake manager gets those lists. The game re-evaluates the few
  passing decisions, then `ai_will_do`, `rand()` and the command, unchanged. A result is used only for
  the same frame, AI object and list version.
- **Shadow (`= 2`):** the game keeps the full lists; before each AI update the list is evaluated again
  on the game thread and compared; mismatches are logged (first 20) and counted in the 30-day summary.
- Requires `cull_decision_checks = 1` and `fast_ai_queue = 1`; off with `decision_profiling`.

## `parallel_events` (2026-10-11; scope bug fixed; shadow run: 0 mismatches in 2.9 M triggers)

`EventManager_DailyScan` (`0x008A7CD0`, stdcall 1, called at `0x006861E8`) evaluates, per country (odd
days: `countries[idx]+0x08` list, scope `+0x08` tag / `+0x0C` index) or owned province (even days:
`province+0x14` list, scope `+0x10` index), each candidate event's `trigger` (embedded `CAndTrigger` at
`event+0x30`); on true: MTTH, one MT draw, maybe `Event_Fire` (`0x008A8120`, thiscall 2), which runs the
immediate effects at once. **[C]**

- Before the scan, the sequence of (trigger block, entity) is listed in scan order and those blocks are
  pointed at a private `CAndTrigger` vtable copy.
- At the scan's first call, chunks of 1024 entries are evaluated on the pool with copies of the scan's
  live scope (taken at chunk time, so the scope parent drawn by `FUN_009B7950` is the real one).
- Each hooked call is answered when it is the next entry (same block and entity); anything else runs
  the original code. Both `Event_Fire` calls are wrapped: evaluations during the fire pass through, and
  afterwards the remaining results are dropped and recomputed. Waste per fire: at most one chunk.
- Shadow (`2`): the original answers every call; the chunk result is compared and mismatches logged.

## Checked and rejected for parallel work

| block | why not |
|---|---|
| `Country_Recalculate` (`0x00515490`) | creates and registers objects (`FUN_005163C0` + vt `+0x28` + `FUN_004A7BC0`), unlinks and deletes objects from world lists (`world+0xA84`, `FUN_005F2AE0`), writes shared relation objects (`+0x38`, `+0x71`). Its caller `Country_PrePass` also writes other countries' relations (`+0x40`/`+0x44`). |
| `WorldMarketUpdate` / `Market_UpdateCountryPops` | an MT draw in the per-state work (`FUN_004A46E0` -> `FUN_004A5170` -> `FUN_009B7950` -> `Rng_Next`); shared static scratch buffers rewritten per country and per pop (`0x013F24F4`…`0x013F25A8`); `UpdateTimeThrottle` inside the loop can render a frame; accumulations clamp at 99 999 (not associative). |

**Scope copies must be relocated.** `CEventScope` points into itself (`+0x24`, the THIS scope, is set
to the object by the ctor). A byte copy of a live scope keeps pointing at the original, so triggers
going through THIS read the original's country. The first `parallel_events` did exactly that (shadow
run: ~0.7 % `serial 1, parallel 0`); fields pointing inside the source are now moved into the copy.
`parallel_decisions` builds its scopes with the ctor and showed 0 mismatches in 135 M checks.
