#pragma once

#include "config.h"

namespace cp::features {

// borderless_fullscreen: the game window is created in the game's own borderless mode (a popup window
// with a windowed Direct3D device) at the primary monitor's size, whatever settings.txt says. Alt-tab
// then switches windows instead of losing and resetting the Direct3D device.
void install_borderless_fullscreen(const Config& config);

// fix_edge_scroll: the map edge-scrolls only while the game is the foreground window and the cursor is
// inside it, not when the cursor sits on another monitor or outside the window. The mouse position is
// measured from the client area (the game assumes a thinner window frame). edge_scroll_lock > 0 also
// holds the cursor in the game view until it is pushed that many mouse counts past an edge.
void install_edge_scroll_fix(const Config& config);

}  // namespace cp::features
