# Victoria II Performance Patch: how each optimisation works

One page per option of `mod/performance_patch.ini`, each with its Source, Why and How. Unless a page says
otherwise, an optimisation gives **the same results** as the unpatched game. Timings:
[timings.md](timings.md).

## Speed

| option | what it does |
|---|---|
| [`skip_network_wait`](skip-network-wait.md) | stops blocking up to 15–35 ms per frame waiting for network packets, also in single player |
| [`cull_decision_checks`](cull-decision-checks.md) | gives each AI country a decision list without the decisions that cannot pass for it |
| [`cull_cb_checks`](cull-cb-checks.md) | skips the AI's casus belli checks that an attacker-only condition already ruled out |
| [`prefetch_relations`](prefetch-relations.md) | prefetches country-pair data before the daily scans that read it |
| [`prefetch_pops`](prefetch-pops.md) | prefetches pops before the daily loops that walk them |
| [`prefetch_ai_relations`](prefetch-ai-relations.md) | prefetches relation data before each AI country's daily target scan |
| [`skip_clean_resets`](skip-clean-resets.md) | skips a daily per-country table reset that would write what is already there |
| [`fast_ai_queue`](fast-ai-queue.md) | at max speed, finishes the AI's daily work in one frame instead of 20 ms per frame |
| [`parallel_decisions`](parallel-decisions.md) | evaluates the AI's daily decision checks on worker threads |
| [`parallel_events`](parallel-events.md) | evaluates the daily event scan's triggers on worker threads |
| [`engine_worker_threads`](engine-worker-threads.md) | thread count of the game's own TBB scheduler |

## Multiplayer

Every player of a game must use the same values.

| option | what it does |
|---|---|
| [`state_checksum`](state-checksum.md) | adds hashes of the game state to the daily sync check, so a desync is reported the day it happens |
| [`oos_log`](oos-log.md) | names the element of the sync check that differed, in `the_grand_loader.log` |
| [`pin_fpu`, `fpu_precision`](pin-fpu.md) | fixes the floating-point control registers at every tick, so all machines compute alike |
| [`fix_rng_resync`](fix-rng-resync.md) | on a sync mismatch, realigns the random generator to the host's position instead of restarting it |
| [`sync_world_rng`](sync-world-rng.md) | takes the lockstep draws of the world's clock-seeded generator from the shared one |
| [`mp_host_wait_days`](mp-host-wait-days.md) | the host waits for the slowest player instead of running ahead until the "lagging behind" warning |

## Memory

| option | what it does |
|---|---|
| [`compress_flag_cache`](compress-flag-cache.md) | keeps the flag atlases compressed (DXT1): ~8× less memory each |

## Diagnostics

Profilers and the yearly timing line: [profilers/](profilers/README.md).

Reverse engineering behind all of this: [re/README.md](re/README.md).
