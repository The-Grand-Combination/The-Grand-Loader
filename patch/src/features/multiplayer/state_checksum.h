#pragma once

#include <cstddef>

#include "config.h"

namespace cp::features {

// Appends hashes of the game state to the world's daily sync vector after every DayTick, so that the
// game's own comparison (host vector carried by the hourly tick command vs. the local one) also
// catches divergence that does not change the number of random draws.
void install_state_checksum(const Config& config);

// Names of the elements of the sync vector, for oos_log: the game's own two, then ours.
const char* sync_element_name(size_t index);

// Index of a named element in the sync vector, or (size_t)-1.
size_t sync_element_index(const char* name);

}  // namespace cp::features
