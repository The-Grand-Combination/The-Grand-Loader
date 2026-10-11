#include <windows.h>

#include "v2cp/plugin_api.h"

#include "config.h"
#include "crash_log.h"
#include "features/cb_cull.h"
#include "features/decision_cull.h"
#include "features/tick_profiler.h"
#include "features/worker_threads.h"
#include "features/year_timer.h"
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

    cp::install_crash_log(&cp::features::tick_profiler_crash_context);
    cp::Config config = cp::load_config(host->config_file);
    cp::features::install_worker_threads(config);
    cp::features::install_cb_cull(config);
    cp::features::install_tick_profiler(config);
    cp::features::install_decision_cull(config);
    cp::features::install_year_timer();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
