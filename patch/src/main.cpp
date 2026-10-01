#include <windows.h>

#include "v2cp/plugin_api.h"

#include "config.h"
#include "features/worker_threads.h"
#include "log.h"

namespace {
const V2CP_HostApi* g_host = nullptr;
}

namespace cp {
const V2CP_HostApi& host() { return *g_host; }
}  // namespace cp

extern "C" __declspec(dllexport) int V2CP_PluginInit(const V2CP_HostApi* host) {
    if (!host || host->api_version != V2CP_PLUGIN_API_VERSION || host->struct_size < sizeof(V2CP_HostApi)) {
        return 1;
    }
    g_host = host;
    cp::logf("Community Patches %s", V2CP_PATCH_VERSION);

    cp::Config config = cp::load_config(host->config_file);
    cp::features::install_worker_threads(config);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
