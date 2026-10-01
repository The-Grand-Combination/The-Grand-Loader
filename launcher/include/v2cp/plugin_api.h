#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V2CP_PLUGIN_API_VERSION 1

// Passed to every plugin. The struct and every string it points to are owned by the loader and stay
// valid for the process lifetime.
typedef struct V2CP_HostApi {
    uint32_t api_version;
    uint32_t struct_size;

    const char* plugin_name;  // .mod file stem, e.g. "community_patch"
    const wchar_t* game_dir;  // directory containing v2game.exe, no trailing separator
    const wchar_t* mod_file;  // absolute path of the .mod that declared this plugin
    const wchar_t* mod_dir;   // absolute path of the .mod's `path =` directory

    void* exe_base;   // HMODULE of v2game.exe; exe_base + RVA == runtime address
    size_t exe_size;  // SizeOfImage

    void (*log)(const struct V2CP_HostApi* host, const char* message);  // one line in v2cp.log
} V2CP_HostApi;

// Exported by each plugin as `V2CP_PluginInit`. Return 0 on success.
// Called from the loader's DllMain (loader lock held): hooking, patching and reading files are fine,
// creating threads or waiting on synchronisation objects is not.
typedef int (*V2CP_PluginInitFn)(const V2CP_HostApi* host);

#ifdef __cplusplus
}
#endif
