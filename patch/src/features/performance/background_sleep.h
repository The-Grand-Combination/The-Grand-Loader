#pragma once

#include "config.h"

namespace cp::features {

// background_sleep_ms: while the Direct3D device is lost (a fullscreen game in the background) the
// main loop sleeps 100 ms per frame, so a multiplayer client that alt-tabs out runs at ten frames
// a second and falls behind. The two sleeps are set to this many milliseconds instead.
void install_background_sleep(const Config& config);

}  // namespace cp::features
