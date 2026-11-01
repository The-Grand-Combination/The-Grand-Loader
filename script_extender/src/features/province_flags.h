#pragma once

#include "config.h"

namespace se::features {

// province_flags: the trigger `has_province_flag = name`, which the engine has a keyword and a
// localisation for but never implemented (it was dropped as an unknown trigger, so it was always true).
// set_province_flag and clr_province_flag already work and are saved.
void install_province_flags(const Config& config);

}  // namespace se::features
