#pragma once

#include "config.h"

namespace se::features::treaty_ui {

// The treaty screens: the plugin's interface/*.gfx and *.gui are parsed with the game's own; the
// diplomacy view's "Justify war" button (CMakeCBAction) first offers "Justify Casus Belli" (vanilla)
// or "Initiate Treaty", which opens the treaty editor (se_treaty_dialog). False when a site does not
// match; nothing is changed then.
bool install(const Config& config);

}  // namespace se::features::treaty_ui
