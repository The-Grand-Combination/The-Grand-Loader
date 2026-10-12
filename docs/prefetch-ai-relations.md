# `prefetch_ai_relations`

## Source

`patch/src/features/performance/relations_prefetch.cpp`. Research: [re/systems/performance.md](re/systems/performance.md).

## Why

Once per AI country per day, `AI_TargetScan` (`0x0084B0F0`, part of the AI's daily job) loops over every
tag, with or without provinces, and tests the relation field `+0x34`. 90 % of its samples sit right after
that read (`0x0084B29D`): one relation cache miss per country per AI country per day.

## How

The function is only reached through its vtable slot (`0x00E2773C`). The slot points to a thunk that
prefetches `+0x34` of the AI country's relation with every tag, then jumps to the original.
