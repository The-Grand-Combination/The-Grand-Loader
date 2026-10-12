# `fast_ai_queue`

**Default 1.** At max speed, lets the AI's daily work finish in one frame. Same jobs, same order.
Source: `patch/src/features/performance/ai_queue.cpp`. Research: [re/systems/tick.md](re/systems/tick.md).

## Why

At the end of each day the game queues one AI job per AI country (`world+0xB84`). Every frame,
`AI_FrameUpdate` (`0x006874C0`) runs jobs for at most **20 ms** of wall time, and the next day is only
issued once the queue is empty. So a day at max speed spans several frames, each paying for a render
and UI pass. The 20 ms budget is wall time, so in vanilla the frame boundaries already depend on the
machine.

## How

The call to `AI_FrameUpdate` (`0x006557C4`) goes to a thunk that, **at max speed only**, calls it again
until the queue is empty (capped at 250 ms per frame so the window stays responsive). The thunk saves
the full FPU/SSE state. Other speeds are unchanged.

It also drives `parallel_decisions`: the batch of decision checks runs at the start of that frame.

## Result

First year (1836): 137 → 133–134 ms/day (two runs; within noise, in the expected direction: fewer
frames per day).
