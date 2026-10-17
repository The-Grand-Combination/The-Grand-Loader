# `parallel_events`

Off by default. A player reported that same-day event chains behaved differently with it on and
normally with it off; the cause is not reproduced yet. `parallel_events = 2` (shadow mode) on the
affected game logs every trigger whose result would have differed, with the event id and the
country or province.

## Source

`patch/src/features/performance/events_parallel.cpp`, `patch/src/parallel/worker_pool.cpp`. Research:
[re/systems/parallel-triggers.md](re/systems/parallel-triggers.md), [re/systems/events.md](re/systems/events.md).

## Why

Every day `EventManager_DailyScan` (`0x008A7CD0`) evaluates the `trigger` of every candidate event of every
country (odd days) or owned province (even days); on true it computes the MTTH, draws a random number and
may fire the event. Triggers only read the game; firing an event runs its effects at once and changes it.

## How

- Two triggers write to the game while they are evaluated, `is_possible_vassal` and
  `can_create_vassals`: each rebuilds a list stored in the country. They are serialised with a lock, so
  two threads never run them at once; before that, two workers on the same country could corrupt the
  heap and crash the game.
- Before the scan, every (trigger, country or province) pair it will evaluate is listed in the scan's
  exact order, and those trigger blocks are pointed at a private copy of their vtable.
- When the scan asks for a trigger, the next chunk of 1024 entries is evaluated on the worker pool with
  copies of the scan's own scope (pointers inside the scope, such as THIS at `+0x24`, are moved into each
  copy), and each call is answered from it as long as it is exactly the next entry; anything else runs the
  original code.
- Only the scan's own two trigger calls (checked by their return address) are answered from the
  batches. Any other evaluation of an event trigger during the scan, from an effect, a nested
  evaluation or code reached after an exception left the scan, runs the original code.
- When an event fires, the results computed before it are dropped and recomputed. The MTTH, the random
  draw and the firing stay on the game thread, in order.
- `2` = shadow mode: the game keeps its own evaluation and the parallel results are compared and logged.
