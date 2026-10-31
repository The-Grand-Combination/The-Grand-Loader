#pragma once

#include "config.h"

namespace cp::features {

// fix_missing_modifier: an add_country_modifier / add_province_modifier / remove_*_modifier effect
// whose modifier name is unknown keeps a null modifier pointer. The game skips it when executing
// but its tooltip builders call through the pointer and crash when the event or decision is shown.
// The guard logs the name at load, gives such an effect an empty tooltip and logs each skipped
// execution. has_country_modifier / has_province_modifier triggers have the same tooltip crash; those
// parsed before the modifiers are loaded (pop types, issues) are looked up again once they are.
void install_modifier_guard(const Config& config);

}  // namespace cp::features
