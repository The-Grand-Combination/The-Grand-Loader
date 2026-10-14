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

bool parse_int(std::string_view value, int& out) {
    return std::from_chars(value.data(), value.data() + value.size(), out).ec == std::errc{};
}

}  // namespace

Config load_config(const std::wstring& config_file) {
    Config config;
    std::ifstream in(config_file);
    std::string raw;
    while (std::getline(in, raw)) {
        std::string_view line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        std::string_view key = trim(line.substr(0, eq));
        int value = 0;
        if (!parse_int(trim(line.substr(eq + 1)), value)) {
            continue;
        }
        for (const Key& k : kKeys) {
            if (key == k.name) {
                if (k.flag) {
                    config.*k.flag = value != 0;
                } else {
                    config.*k.value = value;
                }
                break;
            }
        }
    }

    char line[1024] = "config:";
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
    return config;
}

}  // namespace cp
