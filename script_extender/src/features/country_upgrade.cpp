#include "features/country_upgrade.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "game/localisation.h"
#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Province buildings and the expand click: docs/re/systems/buildings.md.
//
// - CProvinceView_OnExpandClick (0x0073D6D0) gets a pre-hook. With Ctrl and Shift held it posts one
//   CConstructBuildingCommand per eligible province of the player's country, exactly as the vanilla
//   Ctrl path does per province (Province_CanExpandBuilding, then ConstructBuildingCommand_ctor and
//   NetSession_QueueCommand), and returns; otherwise the original handler runs. The commands are
//   validated again on every machine before they execute, so multiplayer stays in sync.
// - The expand tooltip's Localize call for PROVINCE_VIEW_BUILD_ALL_TOOLTIP (0x00747956) gets that text
//   and SE_PROVINCE_VIEW_BUILD_COUNTRY_TOOLTIP on the next line: a key that is not in the table, which
//   Localize shows as it is.

namespace se::features {

namespace {

using game::GameString;
using game::Range;
using game::read;
using game::vector_at;

constexpr uint32_t kExpandClick = 0x0073D6D0;  // CProvinceView_OnExpandClick, thiscall (view, button), RET 4
constexpr unsigned char kExpandClickEntry[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr uint32_t kTooltipLocalize = 0x00747956;  // CALL Localize with PROVINCE_VIEW_BUILD_ALL_TOOLTIP
constexpr uint32_t kLocalize = 0x009A8EE0;         // stdcall (out, const std::string* key), RET 8
constexpr uint32_t kCanExpand = 0x00511E70;        // Province_CanExpandBuilding: EDI = country, (bld, prov), RET 8
constexpr uint32_t kExpandCost = 0x004DA790;  // Building_ComputeExpandCost: EDX = prov, ESI = i64 out, (bld, country, stockpile)
constexpr uint32_t kOperatorNew = 0x00AAE9AF;           // cdecl (size)
constexpr uint32_t kConstructCommandCtor = 0x0057B560;  // stdcall (this, bld, provId, tag, countryIdx), RET 0x14
constexpr uint32_t kConstructCommandSize = 0x58;

// CInGameIdler: +0x1C input (vt[0x30] Ctrl held, vt[0x28] Shift held); vt[0x44] -> +0x38 network session
constexpr uint32_t kIdlerInput = 0x1C;
constexpr uint32_t kInputCtrl = 0x30;
constexpr uint32_t kInputShift = 0x28;
constexpr uint32_t kIdlerSessionOwner = 0x44;
constexpr uint32_t kSessionOwnerSession = 0x38;
constexpr uint32_t kSessionQueueCommand = 0x18;  // NetSession_QueueCommand, thiscall (command)
constexpr uint32_t kButtonRow = 0x80;            // expand button vt[0x80]: row = province-building index
// CBuilding, CProvince, CCountry
constexpr uint32_t kBuildingOnePerState = 0xBE;
constexpr uint32_t kProvinceId = 0x58;
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kProvinceState = 0x188;
constexpr uint32_t kCountryOwnedProvinces = 0x9D8;  // vector<int> of province ids
constexpr uint32_t kCountryMoney = 0xE78;           // i64
constexpr uint32_t kCountryStockpile = 0x1184;

constexpr const char* kBuildAllKey = "PROVINCE_VIEW_BUILD_ALL_TOOLTIP";
constexpr const char* kBuildCountryKey = "SE_PROVINCE_VIEW_BUILD_COUNTRY_TOOLTIP";  // localisation/*.csv

uintptr_t g_trampoline = 0;  // CProvinceView_OnExpandClick minus its replaced entry
bool g_handled = false;      // set by on_expand_click for the thunk; main thread only

using BoolMethod = bool(__fastcall*)(uintptr_t self, void*);
using IntMethod = int(__fastcall*)(uintptr_t self, void*);
using PtrMethod = uintptr_t(__fastcall*)(uintptr_t self, void*);

template <class Fn>
Fn method(uintptr_t object, uint32_t slot) {
    return read<Fn>(read<uintptr_t>(object) + slot);
}

bool can_expand(uintptr_t country, uintptr_t building, uintptr_t province) {
    uintptr_t fn = hooks::exe_va(kCanExpand);
    unsigned char result;
    __asm {
        push edi
        mov edi, country
        push province
        push building
        call fn
        pop edi
        mov result, al
    }
    return result != 0;
}

int64_t expand_cost(uintptr_t country, uintptr_t building, uintptr_t province) {
    uintptr_t fn = hooks::exe_va(kExpandCost);
    uintptr_t stockpile = country + kCountryStockpile;
    int64_t cost = 0;
    int64_t* cost_out = &cost;
    __asm {
        push esi
        mov esi, cost_out
        mov edx, province
        push stockpile
        push country
        push building
        call fn
        pop esi
    }
    return cost;
}

// The command a click on `province`'s expand button posts: ConstructBuildingCommand into the session.
void post_expand(uintptr_t idler, uintptr_t building, uintptr_t province) {
    uintptr_t world = game::world();
    auto* memory = reinterpret_cast<void*(__cdecl*)(size_t)>(hooks::exe_va(kOperatorNew))(kConstructCommandSize);
    if (!memory) {
        return;
    }
    using Ctor = uintptr_t(__stdcall*)(void*, uintptr_t, uint32_t, uint32_t, int);
    uintptr_t command = reinterpret_cast<Ctor>(hooks::exe_va(kConstructCommandCtor))(
        memory, building, read<uint32_t>(province + kProvinceId), read<uint32_t>(world + game::kWorldPlayerTag),
        read<int>(world + game::kWorldPlayerIndex));
    uintptr_t owner = method<PtrMethod>(idler, kIdlerSessionOwner)(idler, nullptr);
    uintptr_t session = read<uintptr_t>(owner + kSessionOwnerSession);
    using Queue = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    method<Queue>(session, kSessionQueueCommand)(session, nullptr, command);
}

// Every province of the player's country where `building` can be expanded, in province-id order, while
// the treasury covers the running total; one per state for a one_per_state building.
void expand_in_country(uintptr_t idler, uintptr_t building) {
    int player = read<int>(game::world() + game::kWorldPlayerIndex);
    uintptr_t country = game::country(player);
    if (!country) {
        return;
    }
    bool one_per_state = read<uint8_t>(building + kBuildingOnePerState) != 0;
    int64_t money = read<int64_t>(country + kCountryMoney);
    std::set<uintptr_t> states;
    int posted = 0;
    Range ids = vector_at(country + kCountryOwnedProvinces);
    for (uintptr_t* id = ids.begin; id < ids.end; ++id) {
        uintptr_t province = game::province(static_cast<uint32_t>(*id));
        if (!province || read<int>(province + kProvinceOwner) != player) {
            continue;
        }
        uintptr_t state = read<uintptr_t>(province + kProvinceState);
        if (one_per_state && states.count(state)) {
            continue;
        }
        if (!can_expand(country, building, province)) {
            continue;
        }
        int64_t cost = expand_cost(country, building, province);
        if (cost > money) {
            continue;
        }
        money -= cost;
        states.insert(state);
        post_expand(idler, building, province);
        ++posted;
    }
    logf("country_upgrade: %s in %d provinces", game::building_name(building).c_str(), posted);
}

// Pre-hook body: true when Ctrl+Shift handled the click.
bool __stdcall on_expand_click(uintptr_t view, uintptr_t button) {
    (void)view;
    uintptr_t idler = read<uintptr_t>(game::world() + game::kWorldIdler);
    uintptr_t input = idler ? read<uintptr_t>(idler + kIdlerInput) : 0;
    if (!input || !method<BoolMethod>(input, kInputCtrl)(input, nullptr) ||
        !method<BoolMethod>(input, kInputShift)(input, nullptr)) {
        return false;
    }
    Range buildings = game::province_buildings();
    int row = method<IntMethod>(button, kButtonRow)(button, nullptr);
    if (row < 0 || static_cast<size_t>(row) >= buildings.size()) {
        return false;
    }
    expand_in_country(idler, buildings.begin[row]);
    return true;
}

// CProvinceView_OnExpandClick entry: ECX = view, [esp+4] = button. Every register kept for the original.
__declspec(naked) void expand_click_thunk() {
    __asm {
        SE_SAVE_ALL
        push dword ptr [esp + SE_SAVED_ALL_BYTES + 4]
        push ecx
        call on_expand_click
        mov g_handled, al
        SE_RESTORE_ALL
        cmp g_handled, 0
        jne handled
        jmp g_trampoline
    handled:
        ret 4
    }
}

using LocalizeFn = void*(__stdcall*)(void* out, const GameString* key);

std::string g_tooltip;  // the composed text; outlives the Localize call it is passed to
bool g_tooltip_read = false;

// Replaces the expand tooltip's Localize call: the build-all line gets the Ctrl+Shift line below it.
void* __stdcall localize_tooltip(void* out, const GameString* key) {
    auto original = reinterpret_cast<LocalizeFn>(hooks::exe_va(kLocalize));
    if (GameString::copy(reinterpret_cast<uintptr_t>(key)) != kBuildAllKey) {
        return original(out, key);
    }
    if (!g_tooltip_read) {
        g_tooltip_read = true;
        auto texts = game::read_localisation({kBuildAllKey, kBuildCountryKey});
        auto all = texts.find(kBuildAllKey);
        auto country = texts.find(kBuildCountryKey);
        if (all != texts.end() && country != texts.end()) {
            g_tooltip = all->second + "\n" + country->second;
        }
    }
    if (g_tooltip.empty()) {
        return original(out, key);  // a text is missing: the vanilla line alone
    }
    GameString text(g_tooltip);
    return original(out, &text);
}

}  // namespace

void install_country_upgrade(const Config& config) {
    if (!config.country_upgrade) {
        return;
    }
    uintptr_t click = hooks::exe_va(kExpandClick);
    if (hooks::expect_bytes(click, kExpandClickEntry, sizeof(kExpandClickEntry))) {
        g_trampoline = hooks::make_trampoline(click, sizeof(kExpandClickEntry));
    }
    if (!g_trampoline || !hooks::jump_hook(click, kExpandClickEntry, sizeof(kExpandClickEntry),
                                           reinterpret_cast<uintptr_t>(&expand_click_thunk))) {
        logf("country_upgrade: CProvinceView_OnExpandClick at %08X is not the expected one, off", kExpandClick);
        return;
    }
    if (!hooks::redirect_call(hooks::exe_va(kTooltipLocalize), hooks::exe_va(kLocalize),
                              reinterpret_cast<uintptr_t>(&localize_tooltip))) {
        logf("country_upgrade: the tooltip call at %08X is not the expected one; no tooltip line",
             kTooltipLocalize);
    }
    logf("country_upgrade: on");
}

}  // namespace se::features
