#pragma once

#include "config.h"

namespace se::features {

// change_flag: the effect `change_flag = "USA/USA_51"` gives the scope's country the flag
// gfx/flags/extra/USA/USA_51.tga (or .dds), whatever its government; `change_flag = default` gives back
// the government's flag. Kept in the save as a country flag. The tooltip names the flag by the localisation
// key USA__USA_51 (the path, / as __) when there is one.
void install_change_flag(const Config& config);

}  // namespace se::features
