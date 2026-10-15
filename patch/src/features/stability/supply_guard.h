#pragma once

#include "config.h"

namespace cp::features {

// fix_supply_crash: the per-country factory walk reads the production type's local-supply source
// without checking it; a type with none crashes the game when that country's production view is
// opened. The guard skips the source-dependent part of the walk for such a factory.
void install_supply_guard(const Config& config);

}  // namespace cp::features
