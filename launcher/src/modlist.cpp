#include "modlist.h"

#include <windows.h>

#include <cwchar>

namespace tgl {

std::vector<PluginEntry> discover_plugins(const std::wstring& game_dir) {
    std::vector<PluginEntry> plugins;
    std::wstring mod_dir = game_dir + L"\\mod";

    WIN32_FIND_DATAW found{};
    HANDLE it = FindFirstFileW((mod_dir + L"\\*.ini").c_str(), &found);
    if (it == INVALID_HANDLE_VALUE) {
        return plugins;
    }
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::wstring file = found.cFileName;
        // The wildcard also matches 8.3 short names (settings.initial -> SETTIN~1.INI).
        if (file.size() <= 4 || _wcsicmp(file.c_str() + file.size() - 4, L".ini") != 0) {
            continue;
        }
        PluginEntry entry;
        entry.name = file.substr(0, file.size() - 4);
        entry.config_file = mod_dir + L"\\" + file;
        entry.plugin_dir = mod_dir + L"\\" + entry.name;
        plugins.push_back(std::move(entry));
    } while (FindNextFileW(it, &found));
    FindClose(it);

    return plugins;
}

}  // namespace tgl
