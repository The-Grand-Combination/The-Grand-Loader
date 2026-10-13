#pragma once

#include "config.h"

namespace cp::features {

// When the game's daily sync check fails, logs which elements of the sync vector differed, with the
// local and the host value. Also logs the game thread's floating-point control words once.
void install_oos_log(const Config& config);

}  // namespace cp::features
