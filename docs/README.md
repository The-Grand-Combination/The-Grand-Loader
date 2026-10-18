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
| [`parallel_events`](parallel-events.md) | off by default: evaluates the daily event scan's triggers on worker threads |
| [`engine_worker_threads`](engine-worker-threads.md) | thread count of the game's own TBB scheduler |
| [`background_sleep_ms`](background-sleep-ms.md) | shortens the 100 ms per-frame sleep of a fullscreen game whose render device is lost (alt-tabbed), so a multiplayer client in the background keeps up |

## Multiplayer

Every player of a game must use the same values.

| option | what it does |
|---|---|
| [`state_checksum`](state-checksum.md) | adds hashes of the game state to the daily sync check, so a desync is reported the day it happens |
| [`oos_log`](oos-log.md) | names the element of the sync check that differed, in `the_grand_loader.log` |
| [`pin_fpu`, `fpu_precision`](pin-fpu.md) | fixes the floating-point control registers at every tick, so all machines compute alike |
| [`fix_rng_resync`](fix-rng-resync.md) | on a sync mismatch, realigns the random generator to the host's position instead of restarting it |
| [`sync_world_rng`](sync-world-rng.md) | takes the lockstep draws of the world's clock-seeded generator from the shared one |
| [`mp_host_wait_days`](mp-host-wait-days.md) | off by default: the host waits for the slowest player instead of running ahead; suspended while a player is at war |
| [`oos_notice`](oos-notice.md) | a recovered out-of-sync is announced in the chat, like the lag warning, instead of the dialog and pause; a real one says what differs |

## Crash prevention and Stability

Guards for crashes of the unpatched game; a game that would not have crashed is unchanged.

| option | what it does |
|---|---|
| [`fix_supply_crash`](fix-supply-crash.md) | production view of a country with a factory whose production type has no local-supply source |
| [`fix_tech_window_crash`](fix-tech-window-crash.md) | technology window with an invention that has no status object |
| [`fix_missing_modifier`](fix-missing-modifier.md) | add/remove modifier effects that name a modifier that does not exist: empty tooltip, no effect, name logged |
| [`fix_missing_state_crash`](fix-missing-state-crash.md) | AI scan over a unit in a province without an owner, which has no state object |
| [`borderless_fullscreen`](borderless-fullscreen.md) | borderless window at the monitor's size instead of exclusive fullscreen: no device loss on alt-tab |
| [`fix_edge_scroll`](fix-edge-scroll.md) | the map edge-scrolls only with the cursor inside the focused game window |

## Interface

| option | what it does |
|---|---|
| [`clipboard_paste`](clipboard-paste.md) | Ctrl+V types the clipboard text into text fields, such as the IP in the join dialog |
| [`save_last_ip`](save-last-ip.md) | saves the join address to `settings.txt`, so the join dialog starts with it next time |
| [`join_port`](join-port.md) | the join dialog accepts `address:port`, for tunnels like playit whose public port is not 1638 |
| [`chat_line_seconds`](chat-line-seconds.md) | how long a chat line stays (vanilla 60 s), the "lagging behind" warning included |

## Memory

| option | what it does |
|---|---|
| [`compress_flag_cache`](compress-flag-cache.md) | keeps the flag atlases compressed (DXT1): ~8× less memory each |

## Diagnostics

Profilers and the yearly timing line: [profilers/](profilers/README.md).

Reverse engineering behind all of this: [re/README.md](re/README.md).
