#pragma once

#include "config.h"

namespace cp::features {

// `parallel_events`: the daily event scan evaluates the `trigger` of every candidate event of every
// country (odd days) or province (even days). Those evaluations are computed in chunks on the worker
// pool, in the scan's order, and handed to the scan as it asks; a fired event (whose effects change the
// game) drops the results computed before it. MTTH, the random draw and the firing stay on the game
// thread. 2 = shadow mode: compare only. See re/systems/parallel-triggers.md.
void install_events_parallel(const Config& config);

}  // namespace cp::features
