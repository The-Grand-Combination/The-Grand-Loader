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
        std::string_view line = raw;
        if (auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        std::string_view value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        fn(trim(line.substr(0, eq)), value);
    }
}

int parse_int(std::string_view value, int fallback) {
    int out = fallback;
    std::from_chars(value.data(), value.data() + value.size(), out);
    return out;
}

}  // namespace

Config load_config(const std::wstring& mod_file) {
    Config config;
    for_each_key(mod_file, [&](std::string_view key, std::string_view value) {
        if (key == "engine_worker_threads") {
            config.engine_worker_threads = parse_int(value, 0);
        }
    });
    logf("config: engine_worker_threads=%d", config.engine_worker_threads);
    return config;
}

}  // namespace cp
