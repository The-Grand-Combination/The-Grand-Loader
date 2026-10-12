#pragma once

#include "config.h"

namespace cp::features {

// `fast_ai_queue`: DayTick queues one AI job per AI country; every frame AI_FrameUpdate works through the
// queue for at most 20 ms, and the next day is only issued once the queue is empty. A game day therefore
// spans several frames, each paying for rendering and UI. At max speed the option lets the queue finish
// in the frame it started in (same jobs, same order). Other speeds are unchanged. See
// re/systems/performance.md.
void install_ai_queue(const Config& config);

}  // namespace cp::features
