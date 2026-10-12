#pragma once

#include "config.h"

namespace cp::features {

// Decision `potential` culling and profiling. Both work through a private copy of the CAndTrigger
// vtable installed on every decision's `potential` block.
//
//   cull_decision_checks: returns false without evaluating when the block cannot pass: a top-level
//                         `tag = X` for a country other than X, or a (country, decision) pair whose
//                         top-level `NOT { has_*_flag }` gate already failed and no flag was cleared since.
//   decision_profiling:   times every decision (1 in 16 sampled) and rewrites performance_patch_decisions.log every
//                         30 game days with running per-day averages for all decisions.
void install_decision_cull(const Config& config);

// parallel_decisions: called by fast_ai_queue on the game thread around a max-speed drain of the AI job
// queue (`queue_ref` = AI_FrameUpdate's argument). Begin evaluates the decision checks of the waiting
// countries on the worker pool; end closes the frame, after which the results are no longer used.
void decisions_frame_begin(void* queue_ref);
void decisions_frame_end();

}  // namespace cp::features
