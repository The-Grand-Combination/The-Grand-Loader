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
        }
    });
    logf("config: engine_worker_threads=%d", config.engine_worker_threads);
    return config;
}

}  // namespace cp
