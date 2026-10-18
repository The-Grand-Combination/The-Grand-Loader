#pragma once

#include "config.h"

namespace cp::features {

// clipboard_paste: Ctrl+V types the clipboard's text into the focused text field (the game's edit
// boxes ignore it), for example an IP address into the multiplayer join dialog.
void install_clipboard_paste(const Config& config);

}  // namespace cp::features
