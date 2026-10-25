#pragma once

#include "config.h"

namespace se::features::treaty_ui {

// The treaty screens: the plugin's interface/*.gfx and *.gui are parsed with the game's own; the
// diplomacy view's "Justify war" button (CMakeCBAction) first offers "Justify Casus Belli" (vanilla)
// or "Initiate Treaty", which opens the treaty editor (se_treaty_dialog). False when a site does not
// match; nothing is changed then.
bool install(const Config& config);

// Puts the texts of the treaty answer messages into the game's localisation table if they are not there
// (cheap when they are). Called before an answer can be shown.
void ensure_message_texts();

// A parser block over the game's cb_types.txt with the plugin's common/se_treaty_cbs.txt appended, for
// CBManager_Load; 0 when there is no such file or the merge failed (then the game's block is used).
uintptr_t merged_cb_block();

// Whether the CB has `po_demand_province = yes` in that text: its term takes one province, not the state.
bool takes_one_province(uintptr_t cb);

}  // namespace se::features::treaty_ui
