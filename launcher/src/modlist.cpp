#include "modlist.h"

#include <windows.h>

#include <shellapi.h>

#include <fstream>
#include <string_view>

namespace v2cp {

namespace {

std::wstring to_wide(std::string_view s) {
    if (s.empty()) {
        return {};
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// Reads the first `key = "value"` from a Paradox-style .mod file; returns empty if absent.
std::string read_mod_key(const std::wstring& mod_file, std::string_view key) {
    std::ifstream in(mod_file);
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
        if (trim(line.substr(0, eq)) != key) {
            continue;
        }
        std::string_view value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return std::string(value);
    }
    return {};
}

std::wstring absolute_in(const std::wstring& base, std::wstring rel) {
    for (auto& c : rel) {
        if (c == L'/') {
            c = L'\\';
        }
    }
    if (rel.size() > 1 && (rel[1] == L':' || rel[0] == L'\\')) {
        return rel;
    }
    return base + L"\\" + rel;
}

std::wstring stem_of(const std::wstring& path) {
    size_t start = path.find_last_of(L"\\/");
    start = start == std::wstring::npos ? 0 : start + 1;
    size_t dot = path.rfind(L'.');
    if (dot == std::wstring::npos || dot < start) {
        dot = path.size();
    }
    return path.substr(start, dot - start);
}

}  // namespace

std::vector<ModEntry> enabled_mods(const std::wstring& game_dir) {
    std::vector<ModEntry> mods;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return mods;
    }

    for (int i = 1; i < argc; ++i) {
        std::wstring_view arg = argv[i];
        if (arg.size() < 5 || _wcsnicmp(arg.data(), L"-mod=", 5) != 0) {
            continue;
        }
        ModEntry entry;
        entry.mod_file = absolute_in(game_dir, std::wstring(arg.substr(5)));
        entry.name = stem_of(entry.mod_file);
        if (std::string path = read_mod_key(entry.mod_file, "path"); !path.empty()) {
            entry.mod_dir = absolute_in(game_dir, to_wide(path));
        }
        mods.push_back(std::move(entry));
    }

    LocalFree(argv);
    return mods;
}

}  // namespace v2cp
