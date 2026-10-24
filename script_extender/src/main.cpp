#include <windows.h>

#include "tgl/plugin_api.h"

#include "config.h"
#include "features/building_map_mode.h"
#include "features/country_upgrade.h"
#include "hooks/memory.h"
#include "log.h"

namespace {
const TGL_HostApi* g_host = nullptr;
}

namespace se {
const TGL_HostApi& host() { return *g_host; }
}  // namespace se

extern "C" __declspec(dllexport) int TGL_PluginInit(const TGL_HostApi* host) {
    if (!host || host->api_version != TGL_PLUGIN_API_VERSION || host->struct_size < sizeof(TGL_HostApi)) {
        return 1;
    }
    g_host = host;
    se::hooks::set_exe_base(host->exe_base);
    se::logf("Victoria II Script Extender for TGC %s", SCRIPT_EXTENDER_VERSION);

    se::Config config = se::load_config(host->config_file);
    se::features::install_building_map_mode(config);
    se::features::install_country_upgrade(config);
    return 0;
}
