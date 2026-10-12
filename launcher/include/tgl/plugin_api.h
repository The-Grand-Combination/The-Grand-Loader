#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TGL_PLUGIN_API_VERSION 1

// Passed to every plugin. The struct and every string it points to are owned by the loader and stay
// valid for the process lifetime.
typedef struct TGL_HostApi {
    uint32_t api_version;
    uint32_t struct_size;

    const char* plugin_name;     // ini file stem, e.g. "performance_patch"
    const wchar_t* game_dir;     // directory containing v2game.exe, no trailing separator
    const wchar_t* config_file;  // absolute path of mod\<name>.ini
    const wchar_t* plugin_dir;   // absolute path of mod\<name>

    void* exe_base;   // HMODULE of v2game.exe; exe_base + RVA == runtime address
    size_t exe_size;  // SizeOfImage

    void (*log)(const struct TGL_HostApi* host, const char* message);  // one line in the_grand_loader.log
} TGL_HostApi;

// Exported by each plugin as `TGL_PluginInit`. Return 0 on success.
// Called from the loader's DllMain (loader lock held): hooking, patching and reading files are fine,
// creating threads or waiting on synchronisation objects is not.
typedef int (*TGL_PluginInitFn)(const TGL_HostApi* host);

#ifdef __cplusplus
}
#endif
