#pragma once

#include "config.h"

namespace cp::features {

// Skips the per-defender `can_use` evaluation of a casus belli type for the rest of the day once an
// attacker-only condition of that block has failed for the attacker. Only active inside the AI's daily
// war/diplomacy target scans; every other caller (UI, console, war planning) sees vanilla behaviour.
void install_cb_cull(const Config& config);

}  // namespace cp::features
