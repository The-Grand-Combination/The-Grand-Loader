#pragma once

#include "config.h"

namespace cp::features {

// network_log: logs every ENet connection and disconnection with the other side's address, and every
// silence of more than two seconds on a connection, to diagnose games that connect but never start.
void install_net_log(const Config& config);

}  // namespace cp::features
