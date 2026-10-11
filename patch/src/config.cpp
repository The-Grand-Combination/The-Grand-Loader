#include "config.h"

#include <charconv>
#include <fstream>
#include <string_view>

#include "log.h"

namespace cp {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

template <typename Fn>
void for_each_key(const std::wstring& file, Fn&& fn) {
    std::ifstream in(file);
    std::string raw;
    while (std::getline(in, raw)) {
        std::string_view line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') {
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        fn(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
}

int parse_int(std::string_view value, int fallback) {
    int out = fallback;
    std::from_chars(value.data(), value.data() + value.size(), out);
    return out;
}

}  // namespace

Config load_config(const std::wstring& config_file) {
    Config config;
    for_each_key(config_file, [&](std::string_view key, std::string_view value) {
        if (key == "engine_worker_threads") {
            config.engine_worker_threads = parse_int(value, 0);
        } else if (key == "cull_cb_checks") {
            config.cull_cb_checks = parse_int(value, 0) != 0;
        } else if (key == "cull_cb_profiling") {
            config.cull_cb_profiling = parse_int(value, 0) != 0;
        } else if (key == "tick_profiler") {
            config.tick_profiler = parse_int(value, 0);
        } else if (key == "cull_decision_checks") {
            config.cull_decision_checks = parse_int(value, 0) != 0;
        } else if (key == "skip_network_wait") {
            config.skip_network_wait = parse_int(value, 0) != 0;
        } else if (key == "sample_profiler") {
            config.sample_profiler = parse_int(value, 0);
        } else if (key == "decision_profiling") {
            config.decision_profiling = parse_int(value, 0) != 0;
        }
    });
    logf("config: engine_worker_threads=%d cull_cb_checks=%d cull_cb_profiling=%d tick_profiler=%d "
         "cull_decision_checks=%d decision_profiling=%d sample_profiler=%d skip_network_wait=%d",
         config.engine_worker_threads, config.cull_cb_checks ? 1 : 0, config.cull_cb_profiling ? 1 : 0,
         config.tick_profiler, config.cull_decision_checks ? 1 : 0, config.decision_profiling ? 1 : 0,
         config.sample_profiler, config.skip_network_wait ? 1 : 0);
    return config;
}

}  // namespace cp
