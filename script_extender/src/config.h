#pragma once

#include <string>

namespace se {

struct Config {
    bool building_map_mode = false;  // infrastructure map mode cycles through the province buildings
    bool country_upgrade = false;    // Ctrl+Shift+click on expand: expand in every province of the country
    bool demand_province = false;    // cb_types.txt `po_demand_province = yes`: the peace takes one province
    bool event_background = false;   // `background = "name"` in an event body: gfx/pictures/events/backgrounds/name
    bool vassal_map_color = false;   // political map: substates and puppets in a shade of their overlord
    int vassal_map_color_own = 30;   // percent of the vassal's own colour in that shade (0-100)
};

// Reads mod/script_extender.ini: `key = value` lines, `;` comments. Unknown keys are logged.
Config load_config(const std::wstring& config_file);

}  // namespace se
