#pragma once

#include "config.h"

namespace cp::features {

// join_port: the multiplayer join dialog accepts "address:port". The game always connects to UDP
// port 1638, which rules out tunnels (playit and the like) whose public port is another one.
void install_join_port(const Config& config);

}  // namespace cp::features
