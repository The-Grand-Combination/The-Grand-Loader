#pragma once

#include <string>
#include <vector>

namespace v2cp {

struct ModEntry {
    std::wstring name;      // file stem, e.g. "community_patch"
    std::wstring mod_file;  // absolute path to the .mod
    std::wstring mod_dir;   // absolute path of `path =`, empty if missing
};

// Resolves every `-mod=` argument of the current process against game_dir.
std::vector<ModEntry> enabled_mods(const std::wstring& game_dir);

}  // namespace v2cp
