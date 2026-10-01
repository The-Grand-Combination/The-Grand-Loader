#pragma once

#include <string>

namespace cp {

struct Config {
    int engine_worker_threads = 0;  // 0 = leave the game default
};

// Reads mod\community_patch.ini.
Config load_config(const std::wstring& config_file);

}  // namespace cp
