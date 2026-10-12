#pragma once

#include "config.h"

namespace cp::features {

// `skip_network_wait`: the game services its ENet host every frame from CommandQueue_Process and blocks
// in select() for up to 15-35 ms waiting for packets, also in single player, where the host is a local
// loopback (~23 % of each day at max speed). With the option on, the poll never blocks: packets are still
// received and dispatched, at most one frame later. See docs/re/systems/network.md.
void install_network_wait(const Config& config);

}  // namespace cp::features
