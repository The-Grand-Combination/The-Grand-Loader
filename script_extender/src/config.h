#pragma once

#include <string>

namespace se {

struct Config {
    bool building_map_mode = false;  // infrastructure map mode cycles through the province buildings
};

// Reads mod/script_extender.ini: `key = value` lines, `;` comments. Unknown keys are logged.
Config load_config(const std::wstring& config_file);

}  // namespace se
