# `fast_ai_queue`

## Source

`patch/src/features/performance/ai_queue.cpp`. Research: [re/systems/tick.md](re/systems/tick.md).

## Why

At the end of each day the game queues one AI job per AI country (`world+0xB84`). Every frame,
`AI_FrameUpdate` (`0x006874C0`) runs jobs for at most 20 ms of wall time, and the next day is only issued
once the queue is empty. So a day at max speed spans several frames, each paying for a render and UI
pass. The budget is wall time, so in vanilla the frame boundaries already depend on the machine.

## How

The call to `AI_FrameUpdate` (`0x006557C4`) goes to a thunk that, at max speed only, calls it again until
the queue is empty (capped at 250 ms per frame so the window stays responsive). Same jobs, same order;
other speeds are unchanged. The thunk saves the full FPU/SSE state and also starts the
`parallel_decisions` batch for that frame.
