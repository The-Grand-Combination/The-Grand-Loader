#pragma once

#include "config.h"

namespace se::features::treaty_ai {

// An AI winner's forced (dismantle) treaty on `loser`, with `budget` points: picks the terms (the land
// rules of docs/re/systems/treaties.md "AI dismantler"), shows them to a human loser and applies them.
// Runs on every machine from the dismantle effects (treaty.cpp take_dismantles); reads synced state only.
void impose(const Config& config, int winner, int loser, int budget);

}  // namespace se::features::treaty_ai
