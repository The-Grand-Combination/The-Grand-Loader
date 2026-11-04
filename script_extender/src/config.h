#pragma once

#include <string>

namespace se {

struct Config {
    bool building_map_mode = false;  // infrastructure map mode cycles through the province buildings
    bool country_upgrade = false;    // Ctrl+Shift+click on expand: expand in every province of the country
    bool buildings_ai = false;       // AI builds what the mod's common/buildings_ai.txt lists
    bool buildings_ai_log = false;   // buildings_ai: log constructions and a summary every 30 days
    bool treaty_dismantle_log = false;  // treaty: log every term an AI dismantler picks and why it was valued so
    bool income_modifiers = false;   // rich/middle/poor_income_modifier scale pop income, country ones too
    bool province_flags = false;     // has_province_flag works (the engine never implemented the trigger)
    bool demand_province = false;    // cb_types.txt `po_demand_province = yes`: the peace takes one province
    bool event_background = false;   // `background = "name"` in an event body: gfx/pictures/events/backgrounds/name
    bool vassal_map_color = false;   // political map: substates and puppets in a shade of their overlord
    int vassal_map_color_own = 30;   // percent of the vassal's own colour in that shade (0-100)
    bool treaty = false;             // peacetime treaties: CBs with `is_treaty_cb = yes` as terms, plus money
    bool treaty_infamy = true;       // a treaty term costs the gaining side the CB's infamy, as a war goal does
    int treaty_ai_reluctance = 10;   // war-score points the AI wants on top of a fair deal
    int treaty_ai_give_weight = 150;  // percent: what it gives counts this much against it
    int treaty_ai_money_per_point = 100000;  // pounds the AI counts as one war-score point
    int treaty_ai_relation = 8;      // points at the best relation (minus at the worst)
    int treaty_ai_core_gain = 2000;  // percent: value of a province of its own cores that it gets
    int treaty_ai_core_give = 4000;  // percent: value of a province of its own cores that it gives away
    int treaty_ai_power = 5;         // points per 100% the proposer is stronger than the AI
    int treaty_ai_power_cap = 300;   // percent: strength advantage counted at most
    int treaty_ai_neighbour = 200;   // percent: the power term when the two border each other
    int treaty_ai_coerce_ratio = 300;  // percent: from this strength ratio a bad relation helps the proposer
    int treaty_ai_hostile = 100;     // percent: how much a bad relation then helps
    int treaty_ai_third_party = 0;   // percent: a gained term for a third country the AI has no stake in
    int treaty_dismantle_leader = 300;     // dismantle effect: points against the leader of a war with the dismantler
    int treaty_dismantle_gp = 200;         // ... on a great power
    int treaty_dismantle_secondary = 150;  // ... on a secondary power
    int treaty_dismantle_other = 100;      // ... on any other loser
    int treaty_dismantle_gw = 200;         // percent: po_dismantle_gw scales the points by this
    int treaty_dismantle_infamy_min = 10;  // hundredths of infamy for the first point over the budget
    int treaty_dismantle_infamy_max = 100;  // hundredths of infamy a point over the budget tends to
    int treaty_dismantle_infamy_half = 50;  // points over the budget at which it is halfway there
    int treaty_dismantle_ai_overspend = 0;  // points over the budget an AI dismantler may impose
    int treaty_dismantle_ai_max_states = 35;  // percent of the loser's non-colonial states an AI takes at most
    int treaty_dismantle_ai_max_new = 3;      // countries an AI dismantler creates at most in one treaty
    int treaty_dismantle_ai_border = 150;    // percent: value of land bordering the gainer or land already its in the treaty
    int treaty_dismantle_ai_adjacent = 120;  // percent: land bordering land the treaty gives to someone else
    int treaty_dismantle_ai_exclave = 0;     // percent: land bordering neither (0 = never; colonies and a new country's first state apart)
    int treaty_dismantle_ai_ally = 130;      // percent: a term that goes to an ally (goes_to_third)
    int treaty_dismantle_ai_home_continent = 130;  // percent: a colony on the continent of the gainer's capital
    std::string treaty_dismantle_ai_coastal_only;  // continents (map/continent.txt names) where the AI takes colonies on the coast only
    std::string treaty_dismantle_ai_near_continents;  // continents where an AI dismantler prefers land near its own
    int treaty_dismantle_ai_near = 50;        // percent added to a land term's value there at distance 0, fading to 0 at near_range
    int treaty_dismantle_ai_near_range = 1500;  // map pixels (map/positions.txt) at which the near bonus is gone
    int treaty_dismantle_ai_far = 50;         // percent of a land term's value there from near_range on
    int treaty_dismantle_ai_colony_repeat = 70;  // percent: each colony already taken on a near or coastal_only continent scales the next
    int treaty_dismantle_ai_infamy = 40;      // percent of the value of a term that costs infamy (land without a claim); 0 = never
    int treaty_dismantle_ai_interest = 200;   // percent: land of the gainer's interest (common/ai_interests.txt)
    int treaty_dismantle_ai_frontier = 150;   // percent: land freed to a third country that borders another country or the sea
    int treaty_dismantle_ai_abroad = 50;      // percent: the loser's homeland off the gainer's continent, next to land it already owned; 0 = never
};

// Reads mod/script_extender.ini: `key = value` lines, `;` comments. Unknown keys are logged.
Config load_config(const std::wstring& config_file);

}  // namespace se
