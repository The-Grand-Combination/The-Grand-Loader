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
        } else if (key == "parallel_events") {
            config.parallel_events = parse_int(value, 0);
        } else if (key == "parallel_decisions") {
            config.parallel_decisions = parse_int(value, 0);
        } else if (key == "fast_ai_queue") {
            config.fast_ai_queue = parse_int(value, 0) != 0;
        } else if (key == "compress_flag_cache") {
            config.compress_flag_cache = parse_int(value, 0) != 0;
        } else if (key == "skip_clean_resets") {
            config.skip_clean_resets = parse_int(value, 0) != 0;
        } else if (key == "prefetch_ai_relations") {
            config.prefetch_ai_relations = parse_int(value, 0) != 0;
        } else if (key == "prefetch_pops") {
            config.prefetch_pops = parse_int(value, 0) != 0;
        } else if (key == "prefetch_relations") {
            config.prefetch_relations = parse_int(value, 0) != 0;
        } else if (key == "skip_network_wait") {
            config.skip_network_wait = parse_int(value, 0) != 0;
        } else if (key == "sample_profiler") {
            config.sample_profiler = parse_int(value, 0);
        } else if (key == "decision_profiling") {
            config.decision_profiling = parse_int(value, 0) != 0;
        } else if (key == "state_checksum") {
            config.state_checksum = parse_int(value, 0) != 0;
        } else if (key == "oos_log") {
            config.oos_log = parse_int(value, 0) != 0;
        } else if (key == "pin_fpu") {
            config.pin_fpu = parse_int(value, 0) != 0;
        } else if (key == "fpu_precision") {
            config.fpu_precision = parse_int(value, 24);
        } else if (key == "oos_test_button") {
            config.oos_test_button = parse_int(value, 0) != 0;
        } else if (key == "fix_rng_resync") {
            config.fix_rng_resync = parse_int(value, 0) != 0;
        } else if (key == "sync_world_rng") {
            config.sync_world_rng = parse_int(value, 0) != 0;
        } else if (key == "mp_host_wait_days") {
            config.mp_host_wait_days = parse_int(value, 0);
        }
    });
    logf("config: engine_worker_threads=%d cull_cb_checks=%d cull_cb_profiling=%d tick_profiler=%d "
         "cull_decision_checks=%d decision_profiling=%d sample_profiler=%d skip_network_wait=%d prefetch_relations=%d prefetch_pops=%d prefetch_ai_relations=%d skip_clean_resets=%d compress_flag_cache=%d fast_ai_queue=%d parallel_decisions=%d parallel_events=%d state_checksum=%d oos_log=%d pin_fpu=%d fpu_precision=%d oos_test_button=%d fix_rng_resync=%d sync_world_rng=%d mp_host_wait_days=%d",
         config.engine_worker_threads, config.cull_cb_checks ? 1 : 0, config.cull_cb_profiling ? 1 : 0,
         config.tick_profiler, config.cull_decision_checks ? 1 : 0, config.decision_profiling ? 1 : 0,
         config.sample_profiler, config.skip_network_wait ? 1 : 0, config.prefetch_relations ? 1 : 0,
         config.prefetch_pops ? 1 : 0, config.prefetch_ai_relations ? 1 : 0, config.skip_clean_resets ? 1 : 0, config.compress_flag_cache ? 1 : 0, config.fast_ai_queue ? 1 : 0, config.parallel_decisions, config.parallel_events,
         config.state_checksum ? 1 : 0, config.oos_log ? 1 : 0, config.pin_fpu ? 1 : 0, config.fpu_precision, config.oos_test_button ? 1 : 0, config.fix_rng_resync ? 1 : 0, config.sync_world_rng ? 1 : 0, config.mp_host_wait_days);
    return config;
}

}  // namespace cp
