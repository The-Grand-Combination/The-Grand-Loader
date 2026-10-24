#pragma once

#include "config.h"

namespace se::features {

// demand_province: the cb_types.txt keyword `po_demand_province = yes`. A CB with it targets a state like
// po_demand_state (same target choice, AI, infamy and texts), but the peace treaty takes only the province
// of the war goal, and its peace cost is that province's alone.
void install_demand_province(const Config& config);

}  // namespace se::features
