#pragma once

#include <string>
#include <vector>

namespace tgl {

struct PluginEntry {
    std::wstring name;         // DLL file stem, e.g. "performance_patch"
    std::wstring dll;          // absolute path of the DLL
    std::wstring config_file;  // absolute path of <name>.ini next to the DLL
    std::wstring plugin_dir;   // absolute path of the DLL's directory
    std::wstring mod_file;     // the .mod file that declared it, as given on the command line
};

// Every `load = "<path>"` line of the .mod files given with -mod= on the command line, in that order.
// The path is relative to mod\ (relative to the game folder when it starts with mod/). A DLL declared
// by several mods is listed once, at its first declaration.
std::vector<PluginEntry> discover_plugins(const std::wstring& game_dir);

}  // namespace tgl
