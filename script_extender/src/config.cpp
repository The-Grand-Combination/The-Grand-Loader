#include "config.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>

#include "log.h"

namespace se {

namespace {

struct Key {
    const char* name;
    bool Config::*flag;  // one of the two is set
    int Config::*value;
};

// Every ini key, in the order of Config. `flag` keys take 0/1, `value` keys an integer.
constexpr Key kKeys[] = {
    {"building_map_mode", &Config::building_map_mode, nullptr},
    {"country_upgrade", &Config::country_upgrade, nullptr},
    {"buildings_ai", &Config::buildings_ai, nullptr},
    {"demand_province", &Config::demand_province, nullptr},
    {"event_background", &Config::event_background, nullptr},
    {"vassal_map_color", &Config::vassal_map_color, nullptr},
    {"vassal_map_color_own", nullptr, &Config::vassal_map_color_own},
    {"treaty", &Config::treaty, nullptr},
    {"treaty_infamy", &Config::treaty_infamy, nullptr},
    {"treaty_ai_reluctance", nullptr, &Config::treaty_ai_reluctance},
    {"treaty_ai_give_weight", nullptr, &Config::treaty_ai_give_weight},
    {"treaty_ai_money_per_point", nullptr, &Config::treaty_ai_money_per_point},
    {"treaty_ai_relation", nullptr, &Config::treaty_ai_relation},
    {"treaty_ai_core_gain", nullptr, &Config::treaty_ai_core_gain},
    {"treaty_ai_core_give", nullptr, &Config::treaty_ai_core_give},
    {"treaty_ai_power", nullptr, &Config::treaty_ai_power},
    {"treaty_ai_power_cap", nullptr, &Config::treaty_ai_power_cap},
    {"treaty_ai_neighbour", nullptr, &Config::treaty_ai_neighbour},
    {"treaty_ai_coerce_ratio", nullptr, &Config::treaty_ai_coerce_ratio},
    {"treaty_ai_hostile", nullptr, &Config::treaty_ai_hostile},
    {"treaty_ai_third_party", nullptr, &Config::treaty_ai_third_party},
    {"treaty_dismantle_leader", nullptr, &Config::treaty_dismantle_leader},
    {"treaty_dismantle_gp", nullptr, &Config::treaty_dismantle_gp},
    {"treaty_dismantle_secondary", nullptr, &Config::treaty_dismantle_secondary},
    {"treaty_dismantle_other", nullptr, &Config::treaty_dismantle_other},
    {"treaty_dismantle_gw", nullptr, &Config::treaty_dismantle_gw},
    {"treaty_dismantle_infamy_min", nullptr, &Config::treaty_dismantle_infamy_min},
    {"treaty_dismantle_infamy_max", nullptr, &Config::treaty_dismantle_infamy_max},
    {"treaty_dismantle_infamy_half", nullptr, &Config::treaty_dismantle_infamy_half},
    {"treaty_dismantle_ai_overspend", nullptr, &Config::treaty_dismantle_ai_overspend},
    {"treaty_dismantle_ai_max_states", nullptr, &Config::treaty_dismantle_ai_max_states},
    {"treaty_dismantle_ai_max_new", nullptr, &Config::treaty_dismantle_ai_max_new},
};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// A whole number, optionally followed by a `;` or `#` comment.
bool parse_int(std::string_view value, int& out) {
    const char* end = value.data() + value.size();
    auto result = std::from_chars(value.data(), end, out);
    if (result.ec != std::errc{}) {
        return false;
    }
    std::string_view rest = trim(std::string_view(result.ptr, static_cast<size_t>(end - result.ptr)));
    return rest.empty() || rest.front() == ';' || rest.front() == '#';
}

const Key* find_key(std::string_view name) {
    for (const Key& k : kKeys) {
        if (name == k.name) {
            return &k;
        }
    }
    return nullptr;
}

// One `key = value` line; comments, sections and blank lines are skipped, anything else unusable is logged.
void parse_line(std::string_view line, Config& config) {
    line = trim(line);
    if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') {
        return;
    }
    size_t eq = line.find('=');
    std::string_view name = trim(line.substr(0, eq));
    const Key* key = eq == std::string_view::npos ? nullptr : find_key(name);
    if (!key) {
        logf("config: unknown key '%.*s' ignored", static_cast<int>(name.size()), name.data());
        return;
    }
    std::string_view text = trim(line.substr(eq + 1));
    int value = 0;
    if (!parse_int(text, value)) {
        logf("config: %s = '%.*s' is not a number, default kept", key->name, static_cast<int>(text.size()),
             text.data());
        return;
    }
    if (key->flag) {
        config.*key->flag = value != 0;
    } else {
        config.*key->value = value;
    }
}

void log_config(const Config& config) {
    char line[2048] = "config:";
    size_t len = std::strlen(line);
    for (const Key& k : kKeys) {
        int value = k.flag ? (config.*k.flag ? 1 : 0) : config.*k.value;
        int n = std::snprintf(line + len, sizeof(line) - len, " %s=%d", k.name, value);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(line) - len) {
            break;
        }
        len += static_cast<size_t>(n);
    }
    logf("%s", line);
}

}  // namespace

Config load_config(const std::wstring& config_file) {
    Config config;
    std::ifstream in(config_file);
    std::string raw;
    while (std::getline(in, raw)) {
        parse_line(raw, config);
    }
    log_config(config);
    return config;
}

}  // namespace se
