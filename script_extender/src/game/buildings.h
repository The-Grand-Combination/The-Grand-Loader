#pragma once

#include <cstdint>

// Province buildings: docs/re/systems/buildings.md.

namespace se::game {

// Province_CanExpandBuilding: controller = owner, the cost within the treasury, and every rule of
// Building_CanBuildInProvince (tech, max level, already building, port, one per state, ...).
bool can_expand(uintptr_t country, uintptr_t building, uintptr_t province);

// Building_ComputeExpandCost for the next level, in the treasury's fixed point (2^15).
int64_t expand_cost(uintptr_t country, uintptr_t building, uintptr_t province);

// Posts the construction as the expand button does: ConstructBuildingCommand into the network session,
// validated again on every machine before it executes. `tag` / `index`: the country that builds.
void post_construction(uintptr_t building, uintptr_t province, uint32_t tag, int index);

}  // namespace se::game
