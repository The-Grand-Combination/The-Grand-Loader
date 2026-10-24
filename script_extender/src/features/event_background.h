#pragma once

#include "config.h"

namespace se::features {

// event_background: `background = "name"` in an event's body replaces the background of its window with
// gfx/pictures/events/backgrounds/name.dds (or .tga), found like a picture: in the mod, then in the game;
// nothing to declare. Without the plugin the line is only logged as an unexpected key and the event keeps
// the default background.
void install_event_background(const Config& config);

}  // namespace se::features
