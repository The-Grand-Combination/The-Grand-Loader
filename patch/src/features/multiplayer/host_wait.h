#pragma once

#include "config.h"

namespace cp::features {

// mp_host_wait_days: the host does not issue a new hourly tick while another player's last reported
// date is more than this many days behind, except while a human-controlled country is at war. Vanilla
// lets the host run ahead until a player is 6 days behind, then lowers the speed and shows "X is
// lagging behind".
void install_host_wait(const Config& config);

}  // namespace cp::features
