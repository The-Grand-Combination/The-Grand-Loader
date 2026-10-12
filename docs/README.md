# Victoria II Performance Patch: how each optimisation works

One page per option of `mod/performance_patch.ini`. Unless a page says otherwise, an optimisation gives
**the same results** as the unpatched game: it only changes how fast the same work is done. First-year
timings of every step: [timings.md](timings.md) (TGC 1836, max speed: 196 ms/day unpatched, 124 with
everything on).

## Speed

| option | what it does | first-year effect |
|---|---|---|
| [`skip_network_wait`](skip-network-wait.md) | stops blocking up to 15–35 ms per frame waiting for network packets, also in single player | −40 to −48 ms/day |
| [`cull_decision_checks`](cull-decision-checks.md) | gives each AI country a decision list without the decisions that cannot pass for it | −7 ms/day |
| [`cull_cb_checks`](cull-cb-checks.md) | skips the AI's casus belli checks that an attacker-only condition already ruled out | up to −11 ms/day |
| [`prefetch_relations`](prefetch-relations.md) | prefetches country-pair data before the daily scans that read it | −7 ms/day |
| [`prefetch_pops`](prefetch-pops.md) | prefetches pops before the daily loops that walk them | part of the last steps |
| [`prefetch_ai_relations`](prefetch-ai-relations.md) | prefetches relation data before each AI country's daily target scan | part of the last steps |
| [`skip_clean_resets`](skip-clean-resets.md) | skips a daily per-country table reset that would write what is already there | late game (pending) |
| [`fast_ai_queue`](fast-ai-queue.md) | at max speed, finishes the AI's daily work in one frame instead of 20 ms per frame | −4 ms/day |
| [`parallel_decisions`](parallel-decisions.md) | evaluates the AI's daily decision checks on worker threads | with the next two: −8 ms/day |
| [`parallel_events`](parallel-events.md) | evaluates the daily event scan's triggers on worker threads | (see above) |
| [`engine_worker_threads`](engine-worker-threads.md) | thread count of the game's own TBB scheduler | not measured |

## Memory

| option | what it does |
|---|---|
| [`compress_flag_cache`](compress-flag-cache.md) | keeps the flag atlases compressed (DXT1): ~8× less memory each |

## Diagnostics

Profilers and the yearly timing line: [profilers/](profilers/README.md).

Reverse engineering behind all of this: [re/README.md](re/README.md).
