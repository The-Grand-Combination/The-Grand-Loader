#pragma once

#include "config.h"

namespace se::features {

// change_flag: the effect `change_flag = "USA/USA_51"` gives the scope's country the flag
// gfx/flags/extra/USA/USA_51.tga (or .dds), whatever its government; `change_flag = default` gives back
// the government's flag. Kept in the save as a country flag.
void install_change_flag(const Config& config);

}  // namespace se::features
