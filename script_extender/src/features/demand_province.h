#pragma once

#include "config.h"

namespace se::features {

// demand_province: the cb_types.txt keyword `po_demand_province = yes`. A CB with it targets a state like
// po_demand_state (same AI, infamy and texts), but the war-goal dialog lists the target's provinces instead of
// its states, the peace treaty takes only the chosen province, and its peace cost is that province's alone.
void install_demand_province(const Config& config);

}  // namespace se::features
