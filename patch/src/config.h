#pragma once

#include <string>

namespace cp {

struct Config {
    int engine_worker_threads = 0;  // 0 = leave the game default
};

// Reads Community Patches keys from the mod's own .mod descriptor.
Config load_config(const std::wstring& mod_file);

}  // namespace cp
