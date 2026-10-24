#pragma once

#include "config.h"

namespace se::features {

// building_map_mode: the infrastructure map mode shows one province building at a time. Clicking its
// button while the mode is shown moves to the next province building of buildings.txt (custom types
// included). Blue: fully built. Blue stripes: the player can expand it. Red stripes: anything else.
void install_building_map_mode(const Config& config);

}  // namespace se::features
