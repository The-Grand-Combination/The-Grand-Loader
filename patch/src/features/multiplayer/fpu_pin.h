#pragma once

#include "config.h"

namespace cp::features {

// Sets the game thread's x87 control word (precision, rounding, exception masks) and MXCSR to fixed
// values at the start of every hourly tick, so every machine of a multiplayer game runs the
// simulation with the same floating-point settings.
void install_fpu_pin(const Config& config);

}  // namespace cp::features
