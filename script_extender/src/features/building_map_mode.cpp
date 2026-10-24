#include "features/building_map_mode.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <d3d9.h>

#include "game/localisation.h"
#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Map modes and province buildings: docs/re/systems/map-modes.md and docs/re/systems/buildings.md.
//
// - The mapmode_6 button glue gets its handler from `MOV EAX,MapModeButton_Infrastructure` at 0x0064B151
//   (CInGameIdler_ctor). The imm32 is replaced, so only clicks and the shortcut reach on_button;
//   CInGameIdler_SetMapMode / _RefreshMapMode still call the original handler directly.
// - The handler's only call of MapColor_Infrastructure (0x0066CABE) is redirected to color_provinces,
//   so every redraw of the mode, refreshes included, uses the selected building.
// - The menubar tooltip looks up MAPMODE_<n> with Localize at 0x0070C5C8. For MAPMODE_6 the call gets
//   SE_MAPMODE_BUILDING_TITLE with $MODE$ and $BUILDING$ replaced (texts from the localisation CSVs)
//   instead: a key that is not in the table, which Localize shows as it is.
// - Every map-mode handler calls MenuBar_SetMapModeButtons(idler, button). A pre-hook there puts a
//   texture with four times thicker stripes into the stripes slot of the texture manager for button 6
//   and the original back for any other; the terrain draw binds that slot as tex7 every frame.

namespace se::features {

namespace {

using game::GameString;
using game::Range;
using game::building_name;
using game::province_buildings;
using game::read;
using game::vector_at;

constexpr uint32_t kHandlerImm = 0x0064B152;      // imm32 of MOV EAX,0x0066C9F0 at 0x0064B151
constexpr uint32_t kInfraHandler = 0x0066C9F0;    // MapModeButton_Infrastructure, thiscall (idler)
constexpr uint32_t kColorCall = 0x0066CABE;       // CALL MapColor_Infrastructure
constexpr uint32_t kInfraColor = 0x008014D0;      // MapColor_Infrastructure, stdcall (CProvinceManager*)
constexpr uint32_t kCanBuildInProvince = 0x00511EF0;
constexpr uint32_t kTooltipLocalize = 0x0070C5C8;  // CALL Localize with the MAPMODE_<n> key
constexpr uint32_t kLocalize = 0x009A8EE0;         // stdcall (out, const std::string* key), RET 8
constexpr uint32_t kSetMapModeButtons = 0x0066C180;  // stdcall (idler, button), RET 8
constexpr unsigned char kSetMapModeButtonsEntry[] = {0x55, 0x8B, 0xEC, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00};
constexpr uint32_t kMapView = 0x012588DC;         // g_mapView

constexpr int kInfrastructureMode = 4;
constexpr int kInfrastructureButton = 6;

// Stripes texture: map view +0x20BC CProvinceManager, whose +0xC0 is the stripes index into the texture
// manager of the render object at +0x3C (entries of 0x2C bytes from +0x15C, IDirect3DTexture9* first;
// device at +0x64).
constexpr uint32_t kViewProvinceManager = 0x20BC;
constexpr uint32_t kManagerRender = 0x3C;
constexpr uint32_t kManagerStripes = 0xC0;
constexpr uint32_t kRenderDevice = 0x64;
constexpr uint32_t kRenderTextures = 0x15C;
constexpr uint32_t kTextureEntrySize = 0x2C;
constexpr int kTextureSlots = 10000;
constexpr UINT kStripeSize = 128;  // one diagonal stripe per 128-texel tile; TGC's stripes.dds has four

// CInGameIdler, CProvinceManager
constexpr uint32_t kIdlerMapMode = 0xDB4;
constexpr uint32_t kManagerRecords = 0x1C;  // vector<record*>: +0 primary ARGB, +4 secondary, +0xC CProvince*
constexpr uint32_t kRecordProvince = 0xC;
// CBuilding, CProvinceBuilding
constexpr uint32_t kBuildingType = 0x10;
constexpr uint32_t kBuildingFileIndex = 0x58;
constexpr uint32_t kBuildingPort = 0xBD;
constexpr uint32_t kBuildingMaxLevel = 0xC4;
constexpr uint32_t kBuildingProvinceIndex = 0x134;
constexpr uint32_t kProvinceBuildingLevel = 0x20;  // x1000
// CProvince
constexpr uint32_t kProvinceTerrain = 0x5C;  // +0x14 -> +0x34: byte[] min_build per building file index
constexpr uint32_t kProvinceBuildings = 0x118;  // vector<CProvinceBuilding*>
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kProvinceController = 0x134;
constexpr uint32_t kProvinceCoastal = 0x158;  // non-zero: a port building can go here
constexpr uint32_t kProvinceConstructions = 0xD8;  // list head
// CBuildingConstruction, CCountry
constexpr uint32_t kConstructionIsBuilding = 0x30;  // vtable slot 12, thiscall () -> bool
constexpr uint32_t kConstructionBuilding = 0x58;
constexpr uint32_t kCountryTechnology = 0xBCC;
constexpr uint32_t kTechMaxLevels = 0x08;  // int[] indexed by CBuilding+0x58 (max_<building>)

constexpr int kRailroadType = 2;

// ARGB. Primary and secondary differ -> the map draws stripes of the two.
constexpr uint32_t kBlue = 0xFF2659D9;
constexpr uint32_t kRed = 0xFFCC3333;
constexpr uint32_t kNeutral = 0xFFB2B266;  // vanilla's khaki: not the player's, or no place for this building

int g_selected = -1;  // index into g_buildingDb+0x1C; -1 until the mode is first drawn

// Building_CanBuildInProvince(country, province, popProject 0, ignoreTech 0, ignoreConstructing 1,
// useBuildingMaxOnly 0, ignoreRailRule 0): EAX = building, RET 0x1C, result in AL.
bool can_build(uintptr_t building, uintptr_t country, uintptr_t province) {
    uintptr_t fn = hooks::exe_va(kCanBuildInProvince);
    unsigned char result;
    __asm {
        push 0
        push 0
        push 1
        push 0
        push 0
        push province
        push country
        mov eax, building
        call fn
        mov result, al
    }
    return result != 0;
}

// Whether `province` has a construction of `building` (its constructions list: nodes {entry, ?, next};
// entry vt[0x30] = is a building construction, entry+0x58 = CBuilding*).
bool is_constructing(uintptr_t province, uintptr_t building) {
    using IsBuildingFn = bool(__fastcall*)(uintptr_t self, void*);
    for (uintptr_t node = read<uintptr_t>(province + kProvinceConstructions); node; node = read<uintptr_t>(node + 8)) {
        uintptr_t entry = read<uintptr_t>(node);
        if (!entry) {
            continue;
        }
        auto is_building = read<IsBuildingFn>(read<uintptr_t>(entry) + kConstructionIsBuilding);
        if (is_building(entry, nullptr) && read<uintptr_t>(entry + kConstructionBuilding) == building) {
            return true;
        }
    }
    return false;
}

// Primary and secondary colour of one province for `building`, as seen by `country` (index `player`).
// Only the player's own provinces are coloured, and for a port building only coastal ones. A level under
// construction counts as built.
void classify(uintptr_t province, uintptr_t building, uintptr_t country, int player, uint32_t& primary,
              uint32_t& secondary) {
    primary = secondary = kNeutral;
    int owner = read<int>(province + kProvinceOwner);
    Range slots = vector_at(province + kProvinceBuildings);
    auto index = read<uint32_t>(building + kBuildingProvinceIndex);
    if (owner != player || index >= slots.size()) {
        return;
    }
    if (read<uint8_t>(building + kBuildingPort) != 0 && read<int>(province + kProvinceCoastal) == 0) {
        return;
    }
    uintptr_t slot = slots.begin[index];
    bool building_now = is_constructing(province, building);
    int level = (slot ? read<int>(slot + kProvinceBuildingLevel) / 1000 : 0) + (building_now ? 1 : 0);

    auto file_index = read<uint32_t>(building + kBuildingFileIndex);
    int reduction = 0;
    uintptr_t terrain = read<uintptr_t>(province + kProvinceTerrain);
    terrain = terrain ? read<uintptr_t>(terrain + 0x14) : 0;
    terrain = terrain ? read<uintptr_t>(terrain + 0x34) : 0;
    if (terrain) {
        reduction = read<uint8_t>(terrain + file_index);
    }
    // The highest level reachable now: max_level, and the tech's max_<building> less the terrain reduction
    // (Building_CanBuildInProvince's limits).
    uintptr_t tech = read<uintptr_t>(country + kCountryTechnology);
    uintptr_t max_levels = tech ? read<uintptr_t>(tech + kTechMaxLevels) : 0;
    int cap = read<int>(building + kBuildingMaxLevel);
    if (max_levels) {
        cap = (std::min)(cap, read<int>(max_levels + file_index * 4) - reduction);
    }
    if (level > 0 && level >= cap) {
        primary = secondary = kBlue;
        return;
    }

    // The game's check sees the level before a construction; `level < cap` already covers the one after.
    bool expandable = read<int>(province + kProvinceController) == owner && can_build(building, country, province);
    primary = expandable ? kBlue : kRed;
    secondary = kNeutral;  // stripes of the colour other countries and inland provinces get
}

// Replaces MapColor_Infrastructure: same signature, same records.
void __stdcall color_provinces(uintptr_t manager) {
    Range buildings = province_buildings();
    if (buildings.size() == 0 || g_selected < 0 || static_cast<size_t>(g_selected) >= buildings.size()) {
        reinterpret_cast<void(__stdcall*)(uintptr_t)>(hooks::exe_va(kInfraColor))(manager);
        return;
    }
    uintptr_t building = buildings.begin[g_selected];
    int player = read<int>(game::world() + game::kWorldPlayerIndex);
    uintptr_t country = game::country(player);

    Range records = vector_at(manager + kManagerRecords);
    for (size_t i = 1; i < records.size(); ++i) {
        uintptr_t record = records.begin[i];
        uintptr_t province = read<uintptr_t>(record + kRecordProvince);
        uint32_t primary = kNeutral;
        uint32_t secondary = kNeutral;
        if (province && country) {
            classify(province, building, country, player, primary, secondary);
        }
        *reinterpret_cast<uint32_t*>(record) = primary;
        *reinterpret_cast<uint32_t*>(record + 4) = secondary;
    }
}

using LocalizeFn = void*(__stdcall*)(void* out, const GameString* key);

// Texts of MAPMODE_6 and of every province building, read from the CSVs on first use. The game keeps
// its localisation only as parsed segments, so the plain text comes from the files.
std::map<std::string, std::string> g_texts;
bool g_texts_read = false;

constexpr const char* kModeKey = "MAPMODE_6";
constexpr const char* kTitleKey = "SE_MAPMODE_BUILDING_TITLE";  // $MODE$ and $BUILDING$ are replaced

void read_texts() {
    if (!g_texts_read) {
        g_texts_read = true;
        std::vector<std::string> keys{kModeKey, kTitleKey};
        Range buildings = province_buildings();
        for (size_t i = 0; i < buildings.size(); ++i) {
            keys.emplace_back(building_name(buildings.begin[i]));
        }
        g_texts = game::read_localisation(keys);
    }
}

std::string localized(const std::string& key) {
    auto it = g_texts.find(key);
    return it != g_texts.end() && !it->second.empty() ? it->second : key;
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

std::string g_title;  // the composed MAPMODE_6 text; outlives the Localize call it is passed to

// Replaces the menubar tooltip's Localize call: the infrastructure title names the shown building.
void* __stdcall localize_tooltip(void* out, const GameString* key) {
    auto original = reinterpret_cast<LocalizeFn>(hooks::exe_va(kLocalize));
    Range buildings = province_buildings();
    if (g_selected < 0 || static_cast<size_t>(g_selected) >= buildings.size() ||
        GameString::copy(reinterpret_cast<uintptr_t>(key)) != kModeKey) {
        return original(out, key);
    }
    read_texts();
    auto title = g_texts.find(kTitleKey);
    if (title == g_texts.end()) {
        return original(out, key);  // no title text: the vanilla one
    }
    g_title = title->second;
    replace_all(g_title, "$MODE$", localized(kModeKey));
    replace_all(g_title, "$BUILDING$", localized(building_name(buildings.begin[g_selected])));
    GameString text(g_title);
    return original(out, &text);
}

IDirect3DTexture9* g_thick = nullptr;    // created on first use, never released (the game may hold it)
IDirect3DTexture9** g_slot = nullptr;    // the texture manager's stripes entry
IDirect3DTexture9* g_original = nullptr;  // what the entry held before the swap
uintptr_t g_trampoline = 0;              // MenuBar_SetMapModeButtons minus its replaced entry

// Alpha 255 (the secondary colour) on 40% of each diagonal period, as in the game's stripes.dds.
IDirect3DTexture9* create_thick_stripes(IDirect3DDevice9* device) {
    IDirect3DTexture9* texture = nullptr;
    if (FAILED(device->CreateTexture(kStripeSize, kStripeSize, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture,
                                     nullptr))) {
        return nullptr;
    }
    D3DLOCKED_RECT rect;
    if (FAILED(texture->LockRect(0, &rect, nullptr, 0))) {
        texture->Release();
        return nullptr;
    }
    for (UINT y = 0; y < kStripeSize; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(rect.pBits) + y * rect.Pitch);
        for (UINT x = 0; x < kStripeSize; ++x) {
            row[x] = (x + y) % kStripeSize < kStripeSize * 2 / 5 ? 0xFFFFFFFF : 0x00FFFFFF;
        }
    }
    texture->UnlockRect(0);
    return texture;
}

// Thick stripes in the stripes entry while `thick`, the game's own otherwise. Main thread.
void set_thick_stripes(bool thick) {
    uintptr_t view = read<uintptr_t>(hooks::exe_va(kMapView));
    uintptr_t manager = view ? view + kViewProvinceManager : 0;
    uintptr_t render = manager ? read<uintptr_t>(manager + kManagerRender) : 0;
    int index = manager ? read<int>(manager + kManagerStripes) : -1;
    if (!render || index < 0 || index >= kTextureSlots) {
        return;
    }
    auto** slot = reinterpret_cast<IDirect3DTexture9**>(render + kRenderTextures + index * kTextureEntrySize);
    if (*slot == nullptr) {
        return;
    }
    if (*slot != g_thick) {
        g_slot = slot;
        g_original = *slot;
    }
    if (!thick) {
        if (g_slot && *g_slot == g_thick) {
            *g_slot = g_original;
        }
        return;
    }
    if (!g_thick) {
        auto* device = read<IDirect3DDevice9*>(render + kRenderDevice);
        g_thick = device ? create_thick_stripes(device) : nullptr;
        if (!g_thick) {
            logf("building_map_mode: could not create the stripes texture");
            return;
        }
    }
    *slot = g_thick;
}

void __stdcall on_map_mode_button(int button) { set_thick_stripes(button == kInfrastructureButton); }

// Pre-hook of MenuBar_SetMapModeButtons(idler, button): every register kept, then the original.
__declspec(naked) void set_buttons_thunk() {
    __asm {
        SE_SAVE_ALL
        push dword ptr [esp + SE_SAVED_ALL_BYTES + 8]
        call on_map_mode_button
        SE_RESTORE_ALL
        jmp g_trampoline
    }
}

// First province building of the railroad type (what vanilla shows), else the first one.
int default_selection(Range buildings) {
    for (size_t i = 0; i < buildings.size(); ++i) {
        if (read<int>(buildings.begin[i] + kBuildingType) == kRailroadType) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// The mapmode_6 button: thiscall (idler), plain RET. Called through the button glue only.
void __fastcall on_button(uintptr_t idler, void*) {
    Range buildings = province_buildings();
    if (buildings.size() != 0) {
        if (g_selected < 0 || static_cast<size_t>(g_selected) >= buildings.size()) {
            g_selected = default_selection(buildings);
        } else if (read<int>(idler + kIdlerMapMode) == kInfrastructureMode) {
            g_selected = static_cast<int>((static_cast<size_t>(g_selected) + 1) % buildings.size());
        }
        logf("building_map_mode: %s", building_name(buildings.begin[g_selected]).c_str());
    }
    reinterpret_cast<void(__fastcall*)(uintptr_t, void*)>(hooks::exe_va(kInfraHandler))(idler, nullptr);
}

}  // namespace

void install_building_map_mode(const Config& config) {
    if (!config.building_map_mode) {
        return;
    }
    if (!hooks::redirect_call(hooks::exe_va(kColorCall), hooks::exe_va(kInfraColor),
                              reinterpret_cast<uintptr_t>(&color_provinces))) {
        logf("building_map_mode: the colour call at %08X is not the expected one, off", kColorCall);
        return;
    }
    if (!hooks::replace_pointer(hooks::exe_va(kHandlerImm), hooks::exe_va(kInfraHandler),
                                reinterpret_cast<uintptr_t>(&on_button))) {
        // Nothing is ever selected then, and color_provinces keeps drawing vanilla's railroad colours.
        logf("building_map_mode: the button handler at %08X is not the expected one; no cycling", kHandlerImm);
    }
    if (!hooks::redirect_call(hooks::exe_va(kTooltipLocalize), hooks::exe_va(kLocalize),
                              reinterpret_cast<uintptr_t>(&localize_tooltip))) {
        logf("building_map_mode: the tooltip call at %08X is not the expected one; vanilla title", kTooltipLocalize);
    }
    uintptr_t buttons = hooks::exe_va(kSetMapModeButtons);
    if (hooks::expect_bytes(buttons, kSetMapModeButtonsEntry, sizeof(kSetMapModeButtonsEntry))) {
        g_trampoline = hooks::make_trampoline(buttons, sizeof(kSetMapModeButtonsEntry));
    }
    if (!g_trampoline || !hooks::jump_hook(buttons, kSetMapModeButtonsEntry, sizeof(kSetMapModeButtonsEntry),
                                           reinterpret_cast<uintptr_t>(&set_buttons_thunk))) {
        logf("building_map_mode: MenuBar_SetMapModeButtons at %08X is not the expected one; thin stripes",
             kSetMapModeButtons);
    }
    logf("building_map_mode: on");
}

}  // namespace se::features
