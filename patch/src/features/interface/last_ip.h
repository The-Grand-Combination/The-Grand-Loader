#pragma once

#include "config.h"

namespace cp::features {

// save_last_ip: the multiplayer join dialog already starts with the "lasthost" setting and the game
// stores the address there on Join, but it writes settings.txt only from a few menus and on a clean
// exit. The settings are written right after the address is stored, so it is there next time.
void install_last_ip(const Config& config);

}  // namespace cp::features
