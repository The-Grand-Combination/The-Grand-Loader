#pragma once

#include "config.h"

namespace se::features {

// vassal_map_color: on the political map a substate or puppet is drawn in a blend of its overlord's colour
// and its own (vassal_map_color_own percent of its own), so it reads as a shade of the overlord. Display
// only: the colour in the country and in saves is untouched.
void install_vassal_map_color(const Config& config);

}  // namespace se::features
