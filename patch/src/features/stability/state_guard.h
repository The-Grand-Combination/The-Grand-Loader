#pragma once

#include "config.h"

namespace cp::features {

// fix_missing_state_crash: a province without an owner has no state object, and two AI helpers
// read a state through the province of a unit without checking for one. An army left standing in
// a province that was uncolonised under it crashes the AI's daily target scan. The guards answer
// "no state" instead.
void install_state_guard(const Config& config);

}  // namespace cp::features
