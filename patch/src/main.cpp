#include <windows.h>

#include "tgl/plugin_api.h"

#include "config.h"
#include "crash_log.h"
#include "features/performance/cb_cull.h"
#include "features/performance/decision_cull.h"
#include "features/profilers/tick_profiler.h"
#include "features/profilers/sample_profiler.h"
#include "features/performance/network_wait.h"
#include "features/performance/relations_prefetch.h"
#include "features/performance/pops_prefetch.h"
#include "features/memory/flag_cache.h"
#include "features/performance/ai_queue.h"
#include "features/performance/events_parallel.h"
#include "features/performance/country_prepass.h"
#include "features/performance/worker_threads.h"
#include "features/profilers/year_timer.h"
#include "log.h"

namespace {
const TGL_HostApi* g_host = nullptr;
}

namespace cp {
const TGL_HostApi& host() { return *g_host; }
}  // namespace cp

extern "C" __declspec(dllexport) int TGL_PluginInit(const TGL_HostApi* host) {
    if (!host || host->api_version != TGL_PLUGIN_API_VERSION || host->struct_size < sizeof(TGL_HostApi)) {
        return 1;
    }
    g_host = host;
    cp::logf("Victoria II Performance Patch %s", PERFORMANCE_PATCH_VERSION);

    cp::install_crash_log(&cp::features::tick_profiler_crash_context);
    cp::Config config = cp::load_config(host->config_file);
    cp::features::install_worker_threads(config);
    cp::features::install_cb_cull(config);
    cp::features::install_tick_profiler(config);
    cp::features::install_decision_cull(config);
    cp::features::install_year_timer();
    cp::features::install_sample_profiler(config);
    cp::features::install_network_wait(config);
    cp::features::install_relations_prefetch(config);
    cp::features::install_ai_relations_prefetch(config);
    cp::features::install_pops_prefetch(config);
    cp::features::install_flag_cache(config);
    cp::features::install_ai_queue(config);
    cp::features::install_events_parallel(config);
    cp::features::install_country_prepass(config);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
