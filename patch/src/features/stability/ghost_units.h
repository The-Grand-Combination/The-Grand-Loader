#pragma once

#include "config.h"

namespace cp::features {

// fix_ghost_units: a tag that loses its last province by any way other than an annexation (secession,
// release, a province transfer) keeps its armies and fleets, standing in other countries' land with no
// owner left to move them. Once a day, before the day's tick, the units of every tag without provinces
// (rebels excepted) are deleted the way an annexation deletes the victim's.
void install_ghost_units(const Config& config);

}  // namespace cp::features
