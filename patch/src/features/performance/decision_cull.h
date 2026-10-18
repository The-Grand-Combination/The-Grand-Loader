#pragma once

#include "config.h"

namespace cp::features {

// Decision culling (decision_cull.cpp), parallel evaluation (decision_parallel.cpp) and profiling
// (decision_profiling.cpp) of the AI's daily decision loops.
//
//   cull_decision_checks: the AI loops see, per country, only the decisions that can pass: a top-level
//                         `tag = X` must be X, a top-level `ai = yes/no` must hold and a top-level
//                         `NOT { has_*_flag }` gate must be true. Lists are rebuilt when a flag is cleared,
//                         after a load and periodically.
//   parallel_decisions:   evaluates those lists on the worker pool in the frames fast_ai_queue drains.
//   decision_profiling:   times every decision's `potential` (1 in 16 sampled) and rewrites
//                         performance_patch_decisions.log every report period with per-day averages.
void install_decision_cull(const Config& config);

// parallel_decisions: called by fast_ai_queue on the game thread around a max-speed drain of the AI job
// queue (`queue_ref` = AI_FrameUpdate's argument). Begin evaluates the decision checks of the waiting
// countries on the worker pool; end closes the frame, after which the results are no longer used.
void decisions_frame_begin(void* queue_ref);
void decisions_frame_end();

}  // namespace cp::features
