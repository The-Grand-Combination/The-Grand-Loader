#pragma once

#include "config.h"

namespace cp::features {

// Forces the thread count the engine passes to its TBB scheduler at startup.
void install_worker_threads(const Config& config);

}  // namespace cp::features
