#include "config.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string_view>

#include "log.h"

namespace cp {

namespace {

struct Key {
    const char* name;
    bool Config::*flag;  // one of the two is set
    int Config::*value;
};

// Every ini key, in the order of Config. `flag` keys take 0/1, `value` keys an integer.
constexpr Key kKeys[] = {
    {"engine_worker_threads", nullptr, &Config::engine_worker_threads},
    {"cull_cb_checks", &Config::cull_cb_checks, nullptr},
    {"cull_cb_profiling", &Config::cull_cb_profiling, nullptr},
    {"tick_profiler", nullptr, &Config::tick_profiler},
    {"cull_decision_checks", &Config::cull_decision_checks, nullptr},
    {"decision_profiling", &Config::decision_profiling, nullptr},
    {"sample_profiler", nullptr, &Config::sample_profiler},
    {"skip_network_wait", &Config::skip_network_wait, nullptr},
    {"prefetch_relations", &Config::prefetch_relations, nullptr},
    {"prefetch_pops", &Config::prefetch_pops, nullptr},
    {"prefetch_ai_relations", &Config::prefetch_ai_relations, nullptr},
    {"skip_clean_resets", &Config::skip_clean_resets, nullptr},
    {"compress_flag_cache", &Config::compress_flag_cache, nullptr},
    {"fast_ai_queue", &Config::fast_ai_queue, nullptr},
    {"parallel_decisions", nullptr, &Config::parallel_decisions},
    {"parallel_events", nullptr, &Config::parallel_events},
    {"state_checksum", &Config::state_checksum, nullptr},
    {"oos_log", &Config::oos_log, nullptr},
    {"pin_fpu", &Config::pin_fpu, nullptr},
    {"fpu_precision", nullptr, &Config::fpu_precision},
    {"oos_test_button", &Config::oos_test_button, nullptr},
    {"fix_rng_resync", &Config::fix_rng_resync, nullptr},
    {"sync_world_rng", &Config::sync_world_rng, nullptr},
    {"mp_host_wait_days", nullptr, &Config::mp_host_wait_days},
    {"background_sleep_ms", nullptr, &Config::background_sleep_ms},
    {"fix_supply_crash", &Config::fix_supply_crash, nullptr},
    {"fix_tech_window_crash", &Config::fix_tech_window_crash, nullptr},
    {"fix_missing_modifier", &Config::fix_missing_modifier, nullptr},
    {"fix_missing_state_crash", &Config::fix_missing_state_crash, nullptr},
    {"clipboard_paste", &Config::clipboard_paste, nullptr},
    {"save_last_ip", &Config::save_last_ip, nullptr},
    {"join_port", &Config::join_port, nullptr},
    {"network_log", &Config::network_log, nullptr},
    {"oos_notice", &Config::oos_notice, nullptr},
    {"chat_line_seconds", nullptr, &Config::chat_line_seconds},
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

}  // namespace cp
