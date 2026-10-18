#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"

namespace cp::features {

// oos_notice: an out-of-sync shows a short notice over the game window instead of the game's dialog
// and pause, when realigning the random generator recovers it (the state hashes match the host's); a
// real divergence keeps the game's reaction and the notice says what differs.
void install_oos_notice(const Config& config);

// Called by the sync check with both vectors, before the game compares its own elements. Returns true
// when the game's dialog and pause should be skipped.
bool oos_notice_on_compare(const uint32_t* local, size_t n_local, const uint32_t* host, size_t n_host);

}  // namespace cp::features
