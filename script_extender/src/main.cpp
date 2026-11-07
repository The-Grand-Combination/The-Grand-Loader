#include <windows.h>

#include "tgl/plugin_api.h"

#include "config.h"
#include "features/building_map_mode.h"
#include "features/buildings_ai.h"
#include "features/change_flag.h"
#include "features/country_upgrade.h"
#include "features/demand_province.h"
#include "features/event_background.h"
#include "features/income_modifiers.h"
#include "features/province_flags.h"
#include "features/treaty.h"
#include "features/vassal_map_color.h"
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
    se::features::install_buildings_ai(config);
    se::features::install_change_flag(config);
    se::features::install_country_upgrade(config);
    se::features::install_demand_province(config);
    se::features::install_event_background(config);
    se::features::install_income_modifiers(config);
    se::features::install_province_flags(config);
    se::features::install_treaty(config);  // after demand_province: it chains onto the same two sites
    se::features::install_vassal_map_color(config);
    return 0;
}
