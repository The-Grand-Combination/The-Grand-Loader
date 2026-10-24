#include "features/vassal_map_color.h"

#include <cstdint>

#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Political colours: docs/re/systems/map-modes.md ("Political colours", "Overlords, substates, puppets").
//
// The owner and controller calls of Country_GetMapColor in MapColor_Political and MapColor_PoliticalPlain
// go through a thunk: the original fills the CColor, then a substate or puppet gets
// overlord * (1 - own) + itself * own. Only these four calls: the function itself also feeds a file
// rewrite (0x0051B870) and UI charts, which keep the real colour.

namespace se::features {

namespace {

using game::read;

constexpr uint32_t kGetMapColor = 0x004FB5C0;  // ESI = country, EAX = out CColor; returns EAX, plain RET
constexpr uint32_t kColorCalls[] = {
    0x007FCAD7,  // MapColor_Political, owner
    0x007FCB22,  // MapColor_Political, controller
    0x007FCC36,  // MapColor_PoliticalPlain, owner
    0x007FCC76,  // MapColor_PoliticalPlain, controller
};
constexpr int kColorCallCount = sizeof(kColorCalls) / sizeof(kColorCalls[0]);

// CCountry
constexpr uint32_t kIsSubstate = 0xCF4;     // u8
constexpr uint32_t kIsVassal = 0xCF5;       // u8, a vassal that is not a substate
constexpr uint32_t kOverlordIndex = 0xCFC;  // with the tag at +0xCF8; 0 = none

// CColor: vtable, ?, floats r g b a, u8 "white"
constexpr uint32_t kColorRed = 0x08;
constexpr uint32_t kColorWhite = 0x18;
constexpr uint32_t kColorSize = 0x1C;
constexpr float kWhiteAbove = 0.999f;  // as CColor_Scale

uintptr_t g_get_color = 0;  // Country_GetMapColor, resolved
float g_own = 0.3f;         // share of the vassal's own colour

void get_color(uintptr_t country, void* color) {  // `out` is reserved in inline asm
    uintptr_t fn = g_get_color;
    __asm {
        mov esi, country
        mov eax, color
        call fn
    }
}

// After the original filled `out` with the colour of `country`.
void __stdcall blend_with_overlord(uintptr_t country, uint8_t* out) {
    if (!country || (!read<uint8_t>(country + kIsSubstate) && !read<uint8_t>(country + kIsVassal))) {
        return;
    }
    int index = read<int>(country + kOverlordIndex);
    uintptr_t overlord = index > 0 ? game::country(index) : 0;
    if (!overlord || overlord == country) {
        return;
    }
    uint8_t top_color[kColorSize];
    get_color(overlord, top_color);
    auto* rgb = reinterpret_cast<float*>(out + kColorRed);
    const auto* top = reinterpret_cast<const float*>(top_color + kColorRed);
    bool white = true;
    for (int i = 0; i < 3; ++i) {
        rgb[i] = top[i] * (1.0f - g_own) + rgb[i] * g_own;
        white = white && rgb[i] > kWhiteAbove;
    }
    out[kColorWhite] = white ? 1 : 0;
}

// Replaces CALL Country_GetMapColor. MapColor_Political keeps its dim factor in XMM1 across the call, so
// everything but what the original changes (ECX, EDX, XMM0) is kept as well.
__declspec(naked) void get_color_thunk() {
    __asm {
        call g_get_color
        SE_SAVE_ALL
        push eax
        push esi
        call blend_with_overlord
        SE_RESTORE_ALL
        ret
    }
}

}  // namespace

void install_vassal_map_color(const Config& config) {
    if (!config.vassal_map_color) {
        return;
    }
    int own = config.vassal_map_color_own < 0 ? 0 : config.vassal_map_color_own > 100 ? 100 : config.vassal_map_color_own;
    g_own = static_cast<float>(own) / 100.0f;
    g_get_color = hooks::exe_va(kGetMapColor);
    int calls = 0;
    for (uint32_t site : kColorCalls) {
        if (hooks::redirect_call(hooks::exe_va(site), g_get_color, reinterpret_cast<uintptr_t>(&get_color_thunk))) {
            ++calls;
        } else {
            logf("vassal_map_color: the Country_GetMapColor call at %08X is not the expected one", site);
        }
    }
    logf("vassal_map_color: on (%d of %d colour calls, %d%% own colour)", calls, kColorCallCount, own);
}

}  // namespace se::features
