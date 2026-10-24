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
    bool treaty = false;             // peacetime treaties: CBs with `is_treaty_cb = yes` as terms, plus money
    bool treaty_infamy = true;       // a treaty term costs the gaining side the CB's infamy, as a war goal does
    int treaty_ai_reluctance = 10;   // war-score points the AI wants on top of a fair deal
    int treaty_ai_give_weight = 150;  // percent: what it gives counts this much against it
    int treaty_ai_money_per_point = 1000;  // pounds the AI counts as one war-score point
    int treaty_ai_relation = 8;      // points at the best relation (minus at the worst)
    int treaty_ai_core_gain = 2000;  // percent: value of a province of its own cores that it gets
    int treaty_ai_core_give = 4000;  // percent: value of a province of its own cores that it gives away
    int treaty_ai_power = 5;         // points per 100% the proposer is stronger than the AI
    int treaty_ai_power_cap = 300;   // percent: strength advantage counted at most
    int treaty_ai_neighbour = 200;   // percent: the power term when the two border each other
    int treaty_ai_coerce_ratio = 300;  // percent: from this strength ratio a bad relation helps the proposer
    int treaty_ai_hostile = 100;     // percent: how much a bad relation then helps
};

// Reads mod/script_extender.ini: `key = value` lines, `;` comments. Unknown keys are logged.
Config load_config(const std::wstring& config_file);

}  // namespace se
