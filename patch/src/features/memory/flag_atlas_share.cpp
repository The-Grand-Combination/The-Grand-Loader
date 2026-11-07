#include "features/memory/flag_atlas_share.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/string.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04, FlagAtlas_CreateAll (FUN_007E0C00; docs/re/systems/flags.md): one flag atlas per
// government of governments.txt (g_governments 0x0131BD5C, in file order). For each government it
// allocates a 0x44-byte atlas, builds or loads `gfx/flags/flagfiles_<flagType>` into it, appends it to the
// atlas vector (device+0x6BD6C) and maps the government's name to its index (device+0x6BDAC), then registers
// its texture with the texture manager. Governments that share a flagType (TGC: 18 governments, 11 flag
// types) each get their own copy of the same texture.
//
//   0x007E0DE1  CALL FlagAtlas_BuildOrLoad   stdcall (atlas, &"gfx/flags", g_countryManager, device,
//                                            &flagType) RET 0x14; flagType is the government's +0x60, ""
//                                            when it has none (flagfiles.tga)
//
// The call is redirected to share_or_build: the first government of each flagType builds as before; a later
// one gets a copy of the first atlas's 0x44 bytes (the same texture, cell table and per-country table) and
// an AddRef on the texture, since the loop registers it with the texture manager again. The builder writes
// nothing but the atlas, and atlases are never destroyed one by one (the teardown, FUN_007E0800, frees
// only the vector), so the shared tables are never freed twice. The pixels are those the government would
// have built: display only, same result.

namespace cp::features {

namespace {

constexpr uint32_t kBuildCallSite = 0x3E0DE1;    // 0x007E0DE1
constexpr uint32_t kBuildOrLoad = 0x3E3900;      // 0x007E3900 FlagAtlas_BuildOrLoad
constexpr size_t kAtlasBytes = 0x44;

using BuildFn = void(__stdcall*)(void* atlas, void* folder, void* countries, void* device, const game::GameString* type);

BuildFn g_build = nullptr;

struct Built {
    std::string type;
    const uint8_t* atlas;
};
std::vector<Built>& g_built() {
    static std::vector<Built> v;
    return v;
}

void __stdcall share_or_build(void* atlas, void* folder, void* countries, void* device, const game::GameString* type) {
    std::string name = type ? std::string(type->c_str(), type->size) : std::string();
    for (const Built& b : g_built()) {
        if (b.type == name) {
            std::memcpy(atlas, b.atlas, kAtlasBytes);
            void* texture = *static_cast<void**>(atlas);  // IDirect3DTexture9*
            if (texture) {
                using AddRefFn = ULONG(__stdcall*)(void*);
                (*static_cast<AddRefFn**>(texture))[1](texture);  // IUnknown::AddRef
            }
            logf("share_flag_atlases: flagfiles%s%s shared with an earlier government", name.empty() ? "" : "_",
                 name.c_str());
            return;
        }
    }
    g_build(atlas, folder, countries, device, type);
    g_built().push_back({name, static_cast<const uint8_t*>(atlas)});
}

}  // namespace

void install_flag_atlas_share(const Config& config) {
    if (!config.share_flag_atlases) {
        logf("share_flag_atlases: disabled");
        return;
    }
    g_build = reinterpret_cast<BuildFn>(hooks::exe_addr(kBuildOrLoad));
    if (!hooks::redirect_call(hooks::exe_addr(kBuildCallSite), hooks::exe_addr(kBuildOrLoad),
                              reinterpret_cast<uintptr_t>(&share_or_build))) {
        logf("share_flag_atlases: site %08X not recognised, off", hooks::listing_va(kBuildCallSite));
        return;
    }
    logf("share_flag_atlases: active, governments with the same flagType share one flag atlas");
}

}  // namespace cp::features
