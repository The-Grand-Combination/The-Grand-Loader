#pragma once

#include "config.h"

namespace cp::features {

// `share_flag_atlases`: the game builds one flag atlas per government, so governments with the same
// flagType hold identical textures (TGC: 18 atlases for 11 flag types). With the option on, they share the
// first one. See docs/re/systems/flags.md.
void install_flag_atlas_share(const Config& config);

}  // namespace cp::features
