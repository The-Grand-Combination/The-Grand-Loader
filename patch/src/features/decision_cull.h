#pragma once

#include "config.h"

namespace cp::features {

// Decision `potential` culling and profiling. Both work through a private copy of the CAndTrigger
// vtable installed on every decision's `potential` block.
//
//   cull_decision_checks: returns false without evaluating when the block cannot pass: a top-level
//                         `tag = X` for a country other than X, or a (country, decision) pair whose
//                         top-level `NOT { has_*_flag }` gate already failed and no flag was cleared since.
//   decision_profiling:   times every decision (1 in 16 sampled) and rewrites v2cp_decisions.log every
//                         30 game days with running per-day averages for all decisions.
void install_decision_cull(const Config& config);

}  // namespace cp::features
