# `parallel_decisions`

## Source

`patch/src/features/performance/decision_cull.cpp`, `patch/src/parallel/worker_pool.cpp`. Research:
[re/systems/parallel-triggers.md](re/systems/parallel-triggers.md).

## Why

Once per AI country per day, `AI_DecisionUpdate` (`0x00821190`) tests `potential` and `allow` of every
decision in its list, then `ai_will_do`, a `rand()` roll and, for a chosen decision, a command that the
game only executes later. So while the AI queue is drained the decision loop does not change the game,
and its checks can be computed in advance, in parallel.

## How

- Two triggers write to the game while they are evaluated, `is_possible_vassal` and
  `can_create_vassals`: each rebuilds a list stored in the country. They are serialised with a lock, so
  two threads never run them at once; before that, two workers on the same country could corrupt the
  heap and crash the game.
- Requires `cull_decision_checks = 1` and `fast_ai_queue = 1`.
- A worker pool (logical processors − 1, at most 7) runs batches with the game thread's FPU state.
- At the start of the frame in which `fast_ai_queue` drains the AI queue, the countries waiting in it are
  read from the queue, and the workers evaluate `potential` and `allow` of every decision of each
  country's list, with the same scope the game builds.
- When the game then runs `AI_DecisionUpdate` for a country, it gets only the decisions that passed, in the
  original order; `ai_will_do`, the `rand()` roll and the command run in the game's own code, so the same
  rolls happen. A result is used only for the same frame, AI object and list version.
- `2` = shadow mode: the game keeps its own evaluation and the parallel results are compared and logged.
