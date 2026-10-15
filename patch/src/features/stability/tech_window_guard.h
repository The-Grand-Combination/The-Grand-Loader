#pragma once

#include "config.h"

namespace cp::features {

// fix_tech_window_crash: the technology window's invention list reads each invention's status
// object through two unchecked pointers, in its sort comparator and in the folder-icon builder.
// The guards treat a missing link as "not less" and as an empty folder name.
void install_tech_window_guard(const Config& config);

}  // namespace cp::features
