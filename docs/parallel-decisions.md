# `parallel_decisions`

**Default 1.** Evaluates the AI's daily decision checks on worker threads. `2` = shadow mode (compare
only). Requires `cull_decision_checks = 1` and `fast_ai_queue = 1`.
Source: `patch/src/features/performance/decision_cull.cpp`, `patch/src/parallel/worker_pool.cpp`.
Research: [re/systems/parallel-triggers.md](re/systems/parallel-triggers.md).

## Why

Once per AI country per day, `AI_DecisionUpdate` (`0x00821190`) tests `potential` and `allow` of every
decision in its list, then `ai_will_do`, a `rand()` roll and, for a chosen decision, a **command** that the
game only executes later. So while the AI queue is drained, the decision loop does not change the game:
its checks can be computed in advance, in parallel.

## How

- A worker pool (logical processors − 1, at most 7) runs batches with the game thread's FPU state.
- At the start of the frame in which `fast_ai_queue` drains the AI queue, the countries waiting in it are
  read from the queue, and the workers evaluate `potential` and `allow` of every decision of each
  country's (already culled) list, with the same scope the game builds.
- When the game then runs `AI_DecisionUpdate` for a country, it gets a list of only the decisions that
  passed, in the original order. `ai_will_do`, the `rand()` roll and the command run as before, in the
  game's own code, so the same rolls happen. A result is used only for the same frame, AI object and list
  version.
- `2` (shadow): the game keeps its own evaluation; the parallel results are compared and mismatches
  logged.

## Result

Shadow mode: **0 mismatches in 162 M checks** (1836–1837). ~300 k checks a day cost ~1.5 ms on the game
thread. Together with `parallel_events` and `prefetch_ai_relations`: 132 → **124 ms/day** in the first
year.
