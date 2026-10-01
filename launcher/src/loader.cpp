#include "loader.h"

#include <windows.h>

#include <psapi.h>

#include <string>
#include <vector>

#include "v2cp/plugin_api.h"

#include "log.h"
#include "modlist.h"

namespace v2cp {

namespace {

constexpr std::string_view kLoaderTag = "loader";

struct LoadedPlugin {
    V2CP_HostApi api;
    std::string name;
    std::wstring game_dir;
    std::wstring config_file;
    std::wstring plugin_dir;
};

// Plugins keep the HostApi pointer, so each plugin is heap-allocated once and never moved or freed.
std::vector<LoadedPlugin*> g_plugins;

std::string narrow(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring exe_path() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    path.resize(n);
    return path;
}

bool is_target_process(const std::wstring& exe) {
    size_t slash = exe.find_last_of(L"\\/");
    std::wstring file = slash == std::wstring::npos ? exe : exe.substr(slash + 1);
    return _wcsicmp(file.c_str(), L"v2game.exe") == 0;
}

void plugin_log(const V2CP_HostApi* host, const char* message) {
    log::line(host && host->plugin_name ? host->plugin_name : "plugin", message ? message : "");
}

void load_plugin(const PluginEntry& entry, const std::wstring& game_dir) {
    std::wstring dll = entry.plugin_dir + L"\\" + entry.name + L".dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        log::linef(kLoaderTag, "%s: no %s, ignoring", narrow(entry.name).c_str(), narrow(dll).c_str());
        return;
    }

    HMODULE module = LoadLibraryW(dll.c_str());
    if (!module) {
        log::linef(kLoaderTag, "failed to load %s (error %lu)", narrow(dll).c_str(), GetLastError());
        return;
    }
    auto init = reinterpret_cast<V2CP_PluginInitFn>(GetProcAddress(module, "V2CP_PluginInit"));
    if (!init) {
        log::linef(kLoaderTag, "%s does not export V2CP_PluginInit, ignoring", narrow(entry.name).c_str());
        FreeLibrary(module);
        return;
    }

    MODULEINFO info{};
    GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &info, sizeof(info));

    auto* plugin = new LoadedPlugin{};
    plugin->name = narrow(entry.name);
    plugin->game_dir = game_dir;
    plugin->config_file = entry.config_file;
    plugin->plugin_dir = entry.plugin_dir;
    plugin->api = V2CP_HostApi{
        .api_version = V2CP_PLUGIN_API_VERSION,
        .struct_size = sizeof(V2CP_HostApi),
        .plugin_name = plugin->name.c_str(),
        .game_dir = plugin->game_dir.c_str(),
        .config_file = plugin->config_file.c_str(),
        .plugin_dir = plugin->plugin_dir.c_str(),
        .exe_base = info.lpBaseOfDll,
        .exe_size = info.SizeOfImage,
        .log = plugin_log,
    };
    g_plugins.push_back(plugin);

    log::linef(kLoaderTag, "loading %s", narrow(dll).c_str());
    int rc = init(&plugin->api);
    if (rc != 0) {
        log::linef(kLoaderTag, "%s returned %d from V2CP_PluginInit", plugin->name.c_str(), rc);
    }
}

}  // namespace

void loader_init() {
    std::wstring exe = exe_path();
    if (!is_target_process(exe)) {
        return;
    }
    std::wstring game_dir = exe.substr(0, exe.find_last_of(L"\\/"));

    log::open(game_dir + L"\\v2cp.log");
    log::linef(kLoaderTag, "Community Patches loader %s", V2CP_LOADER_VERSION);

    std::vector<PluginEntry> entries = discover_plugins(game_dir);
    log::linef(kLoaderTag, "%zu plugin declaration(s) in mod\\*.ini", entries.size());
    for (const PluginEntry& entry : entries) {
        load_plugin(entry, game_dir);
    }
    log::linef(kLoaderTag, "%zu plugin(s) loaded", g_plugins.size());
}

}  // namespace v2cp
