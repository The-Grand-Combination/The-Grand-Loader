#include "modlist.h"

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <fstream>
#include <string_view>

#include "log.h"

namespace tgl {

namespace {

constexpr std::string_view kLoaderTag = "loader";

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

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

// The .mod files are in the system code page, as the game reads them.
std::wstring widen(std::string_view s) {
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

bool starts_with_mod(const std::wstring& path) {
    return path.size() > 4 && _wcsnicmp(path.c_str(), L"mod", 3) == 0 && (path[3] == L'\\' || path[3] == L'/');
}

std::wstring full_path(const std::wstring& path) {
    std::wstring out(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetFullPathNameW(path.c_str(), static_cast<DWORD>(out.size()), out.data(), nullptr);
        if (n == 0) {
            return path;
        }
        if (n < out.size()) {
            out.resize(n);
            return out;
        }
        out.resize(n);
    }
}

bool already_declared(const std::vector<PluginEntry>& plugins, const std::wstring& dll) {
    return std::any_of(plugins.begin(), plugins.end(),
                       [&](const PluginEntry& p) { return _wcsicmp(p.dll.c_str(), dll.c_str()) == 0; });
}

// Every `load = "<path>"` of one .mod file (mod_file as given with -mod=, relative to the game folder).
void read_mod_file(const std::wstring& game_dir, const std::wstring& mod_file, std::vector<PluginEntry>& plugins) {
    std::wstring file = mod_file;
    std::replace(file.begin(), file.end(), L'/', L'\\');
    bool absolute = file.size() > 1 && (file[1] == L':' || file[0] == L'\\');
    std::ifstream in(absolute ? file : game_dir + L"\\" + file);
    if (!in) {
        log::linef(kLoaderTag, "%s: cannot be read, no plugins from it", log::narrow(mod_file).c_str());
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string_view text = trim(strip_comment(line));  // `load = "x.dll" # comment` is allowed
        size_t eq = text.find('=');
        if (text.empty() || eq == std::string_view::npos) {
            continue;
        }
        if (trim(text.substr(0, eq)) != "load") {
            continue;
        }
        std::string_view value = trim(text.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (value.empty()) {
            continue;
        }
        std::wstring relative = widen(value);
        std::replace(relative.begin(), relative.end(), L'/', L'\\');
        std::wstring dll = full_path(game_dir + (starts_with_mod(relative) ? L"\\" : L"\\mod\\") + relative);
        if (already_declared(plugins, dll)) {
            log::linef(kLoaderTag, "%s: %s already declared, loaded once", log::narrow(mod_file).c_str(),
                       log::narrow(dll).c_str());
            continue;
        }
        size_t slash = dll.find_last_of(L'\\');
        std::wstring base = slash == std::wstring::npos ? dll : dll.substr(slash + 1);
        size_t dot = base.find_last_of(L'.');
        PluginEntry entry;
        entry.name = dot == std::wstring::npos ? base : base.substr(0, dot);
        entry.dll = dll;
        entry.plugin_dir = slash == std::wstring::npos ? game_dir : dll.substr(0, slash);
        entry.config_file = entry.plugin_dir + L"\\" + entry.name + L".ini";
        entry.mod_file = mod_file;
        plugins.push_back(std::move(entry));
    }
}

}  // namespace

std::vector<PluginEntry> discover_plugins(const std::wstring& game_dir) {
    std::vector<PluginEntry> plugins;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    size_t mods = 0;
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring arg = argv[i];
        if (_wcsnicmp(arg.c_str(), L"-mod=", 5) == 0) {
            mods++;
            read_mod_file(game_dir, arg.substr(5), plugins);
        }
    }
    LocalFree(argv);
    if (mods == 0) {
        log::line(kLoaderTag, "no -mod= on the command line: plugins are declared by load = in a .mod file");
    }
    return plugins;
}

}  // namespace tgl
