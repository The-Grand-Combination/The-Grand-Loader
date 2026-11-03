#pragma once

#include "config.h"

namespace se::features {

// country_upgrade: Ctrl+Shift+click on a province building's expand button posts the expansion for every
// province of the player's country where it can be expanded, as long as the treasury covers them
// (vanilla's Ctrl+click covers one state). The expand tooltip gets a line explaining it.
void install_country_upgrade(const Config& config);

}  // namespace se::features
