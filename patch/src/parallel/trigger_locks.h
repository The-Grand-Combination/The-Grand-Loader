#pragma once

#include "config.h"

namespace cp::parallel {

// Triggers that write to the game while they are evaluated are serialised with one lock, so the
// features that evaluate triggers on worker threads (parallel_decisions, parallel_events) cannot run
// two of them on the same object at once. Installed when either feature is on.
void install_trigger_locks(const Config& config);

}  // namespace cp::parallel
