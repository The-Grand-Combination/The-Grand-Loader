#pragma once

#include <string>
#include <vector>

namespace tgl {

struct PluginEntry {
    std::wstring name;         // ini file stem, e.g. "performance_patch"
    std::wstring config_file;  // absolute path to mod\<name>.ini
    std::wstring plugin_dir;   // absolute path to mod\<name>
};

// Every mod\*.ini in game_dir is a plugin declaration; the DLL is expected at mod\<name>\<name>.dll.
std::vector<PluginEntry> discover_plugins(const std::wstring& game_dir);

}  // namespace tgl
