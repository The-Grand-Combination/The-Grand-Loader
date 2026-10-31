#pragma once

#include "config.h"

namespace se::features {

// buildings_ai: the AI builds any province building listed in the mod's common/buildings_ai.txt, with a
// chance per run of its construction job, a treasury margin, and a province weight written like an
// ai_will_do (factor and modifiers, province scope). The vanilla builders keep their fort, naval base
// and railroad, now found by name instead of by their place in buildings.txt. Engine facts:
// docs/re/systems/buildings.md ("The AI builder").
void install_buildings_ai(const Config& config);

}  // namespace se::features
