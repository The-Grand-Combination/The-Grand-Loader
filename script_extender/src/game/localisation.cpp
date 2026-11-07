#include "game/localisation.h"

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>

#include "log.h"

namespace se::game {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

struct ModFolders {
    std::vector<std::wstring> folders;  // absolute, in load order
    bool replaces_localisation = false;
};

// The line up to its `#` comment; a `#` inside quotes is part of the value.
std::string_view strip_comment(std::string_view line) {
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            quoted = !quoted;
        } else if (line[i] == '#' && !quoted) {
            return line.substr(0, i);
        }
    }
    return line;
}

// `path = "mod/X"` and `replace_path = "localisation"` of one .mod file (as given with -mod=: relative to
// the game folder, or absolute). Mods put comments after the values (`path = "mod/X" # location ...`).
void read_mod_file(const std::wstring& game_dir, const std::wstring& mod_file, ModFolders& out) {
    std::wstring file = mod_file;
    std::replace(file.begin(), file.end(), L'/', L'\\');
    bool absolute = file.size() > 1 && (file[1] == L':' || file[0] == L'\\');
    std::ifstream in(absolute ? file : game_dir + L"\\" + file);
    std::string line;
    while (std::getline(in, line)) {
        std::string_view text = trim(strip_comment(line));
        size_t eq = text.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        std::string_view key = trim(text.substr(0, eq));
        std::string_view value = trim(text.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (key == "path") {
            std::wstring folder = game_dir + L"\\" + std::wstring(value.begin(), value.end());
            std::replace(folder.begin(), folder.end(), L'/', L'\\');
            out.folders.push_back(folder);
        } else if (key == "replace_path" && value == "localisation") {
            out.replaces_localisation = true;
        }
    }
}

ModFolders mod_folders(const std::wstring& game_dir) {
    ModFolders out;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg.rfind(L"-mod=", 0) == 0) {
            read_mod_file(game_dir, arg.substr(5), out);
        }
    }
    LocalFree(argv);
    return out;
}

// *.csv of `folder`, sorted by name (the game's order).
std::vector<std::wstring> csv_files(const std::wstring& folder) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((folder + L"\\*.csv").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return names;
    }
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            names.push_back(data.cFileName);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

std::map<std::string, std::string> read_localisation(const std::vector<std::string>& keys) {
    std::map<std::string, std::string> found;
    std::set<std::string> wanted(keys.begin(), keys.end());

    std::wstring game_dir = host().game_dir;
    ModFolders mods = mod_folders(game_dir);
    std::vector<std::wstring> folders;
    for (const std::wstring& mod : mods.folders) {
        folders.push_back(mod + L"\\localisation");
    }
    if (!mods.replaces_localisation) {
        folders.push_back(game_dir + L"\\localisation");
    }
    folders.push_back(std::wstring(host().plugin_dir) + L"\\localisation");  // ours last: a mod may override

    std::set<std::wstring> seen_files;
    for (const std::wstring& folder : folders) {
        for (const std::wstring& name : csv_files(folder)) {
            if (!seen_files.insert(name).second) {
                continue;  // replaced by a mod file of the same name
            }
            std::ifstream in(folder + L"\\" + name, std::ios::binary);
            std::string line;
            while (std::getline(in, line) && found.size() < wanted.size()) {
                size_t first = line.find(';');
                if (first == std::string::npos) {
                    continue;
                }
                std::string key = line.substr(0, first);
                if (!wanted.count(key) || found.count(key)) {
                    continue;
                }
                size_t second = line.find(';', first + 1);
                std::string_view text(line.data() + first + 1,
                                      (second == std::string::npos ? line.size() : second) - first - 1);
                // The game's loader turns "\n" into a newline; text passed as a missing key is not.
                std::string value(trim(text));
                for (size_t at = value.find("\\n"); at != std::string::npos; at = value.find("\\n", at + 1)) {
                    value.replace(at, 2, "\n");
                }
                found[key] = value;
            }
        }
    }
    return found;
}

std::wstring game_file(const std::wstring& relative) {
    std::wstring game_dir = host().game_dir;
    for (const std::wstring& folder : mod_folders(game_dir).folders) {
        std::wstring path = folder + L"\\" + relative;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    std::wstring path = game_dir + L"\\" + relative;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? path : std::wstring();
}

}  // namespace se::game
