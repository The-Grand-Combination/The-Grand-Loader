#include <windows.h>

#include "tgl/plugin_api.h"

#include "config.h"
#include "crash_log.h"
#include "features/memory/flag_cache.h"
#include "features/multiplayer/fpu_pin.h"
#include "features/multiplayer/host_wait.h"
#include "features/multiplayer/oos_log.h"
#include "features/multiplayer/oos_test.h"
#include "features/multiplayer/state_checksum.h"
#include "features/multiplayer/sync_check.h"
#include "features/multiplayer/world_rng.h"
#include "features/performance/ai_queue.h"
#include "features/performance/background_sleep.h"
#include "features/performance/cb_cull.h"
#include "features/performance/country_prepass.h"
#include "features/performance/decision_cull.h"
#include "features/performance/events_parallel.h"
#include "features/performance/network_wait.h"
#include "features/performance/pops_prefetch.h"
#include "features/performance/relations_prefetch.h"
#include "features/performance/worker_threads.h"
#include "features/profilers/sample_profiler.h"
#include "features/stability/modifier_guard.h"
#include "features/stability/state_guard.h"
#include "features/stability/supply_guard.h"
#include "features/stability/tech_window_guard.h"
#include "features/profilers/tick_profiler.h"
#include "features/profilers/year_timer.h"
#include "hooks/memory.h"
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
    cp::hooks::set_exe_base(host->exe_base);
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
    cp::features::install_background_sleep(config);
    cp::features::install_relations_prefetch(config);
    cp::features::install_ai_relations_prefetch(config);
    cp::features::install_pops_prefetch(config);
    cp::features::install_flag_cache(config);
    cp::features::install_ai_queue(config);
    cp::features::install_events_parallel(config);
    cp::features::install_country_prepass(config);
    // after tick_profiler: state_checksum chains to whatever the DayTick call site holds
    cp::features::install_state_checksum(config);
    cp::features::install_oos_log(config);
    cp::features::install_sync_check(config);
    cp::features::install_world_rng(config);
    cp::features::install_host_wait(config);
    cp::features::install_fpu_pin(config);
    cp::features::install_oos_test(config);
    cp::features::install_supply_guard(config);
    cp::features::install_tech_window_guard(config);
    cp::features::install_modifier_guard(config);
    cp::features::install_state_guard(config);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
