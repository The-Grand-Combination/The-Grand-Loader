#include "features/province_flags.h"

#include <cstdint>

#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// has_province_flag (token 0x43B), the trigger the engine never implemented:
//
// - set_province_flag / clr_province_flag (CSetProvinceFlagEffect_Execute 0x00882A90,
//   CClrProvinceFlagEffect_Execute 0x00882B00) set and clear a flag in the CFlags of the scope's province
//   (province+0x328; lookup = its vtable slot 2, thiscall (const char* name), the entry's u8 +0x1C = set).
//   The province save writes them under `flags` and the loader reads them back.
// - The trigger factory (TriggerBlock_AddChild 0x008B33E0, slot 4 of every trigger block: thiscall
//   (block, parser, token), RET 8) has a case for has_country_flag (0x40A) and has_global_flag (0x79D) but
//   none for 0x43B: the default case logs an unknown trigger and appends nothing, so the condition is
//   always true, inside NOT too (an empty NOT is true).
//
// The factory entry (`55 8B EC 83 E4 F8`, like the effect factory) jumps to trigger_factory: for 0x43B it
// builds a has_country_flag trigger (CHasCountryFlagTrigger, 0x38 bytes, flag name std::string at +0x1C,
// appended to the block's vector at +0x08/+0x0C and parsed by the factory as usual), then points it at a
// copy of that vtable whose Evaluate (slot 6) looks the name up in the province of the scope
// (CEventScope+0x10; false when there is none, as CTerrainTrigger does) and whose tooltip (slot 10) is
// the has_country_flag one with the key HAVE_PROVINCE_FLAG / HAVE_NOT_PROVINCE_FLAG (vanilla
// localisation; the exe never references them): its two std::string::assign calls of the key
// (0x008CCC8A, 0x008CCF37) go to key_thunk, which swaps the key while this thread is in our tooltip.
// Reads only; the result depends on game state alone, so it is the same on every machine.

namespace se::features {

namespace {

using game::read;

constexpr uint32_t kTriggerFactory = 0x008B33E0;  // TriggerBlock_AddChild
constexpr unsigned char kTriggerFactoryEntry[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr int kTokenHasProvinceFlag = 0x43B;
constexpr int kTokenHasCountryFlag = 0x40A;
constexpr uint32_t kTriggerBlockEnd = 0x0C;  // vector<CTrigger*> {begin +0x08, end +0x0C, capacity}

constexpr uint32_t kHasCountryFlagVtable = 0x00E307E8;  // CHasCountryFlagTrigger::vftable
constexpr uint32_t kHasCountryFlagTooltip = 0x008CCBF0;
constexpr uint32_t kTriggerSlots = 13;
constexpr uint32_t kSlotEvaluate = 6;
constexpr uint32_t kSlotTooltip = 10;
constexpr uint32_t kTriggerFlagName = 0x1C;  // std::string

constexpr uint32_t kStringAssign = 0x00409350;  // std::string::assign, thiscall (const char*, size), RET 8
constexpr uint32_t kKeyCalls[] = {0x008CCC8A, 0x008CCF37};  // HAVE_NOT_COUNTRY_FLAG, HAVE_COUNTRY_FLAG
constexpr uint32_t kKeyHaveNot = 0x00E2D93C;  // "HAVE_NOT_COUNTRY_FLAG"
constexpr uint32_t kKeyHave = 0x00E2D954;     // "HAVE_COUNTRY_FLAG"
constexpr char kProvinceHaveNot[] = "HAVE_NOT_PROVINCE_FLAG";
constexpr char kProvinceHave[] = "HAVE_PROVINCE_FLAG";

constexpr uint32_t kScopeProvince = 0x10;
constexpr uint32_t kProvinceFlags = 0x328;  // CFlags
constexpr uint32_t kFlagsFind = 2;          // CFlags slot 2: thiscall (const char* name) -> entry or 0
constexpr uint32_t kFlagSet = 0x1C;         // u8 in the entry

uintptr_t g_factory_trampoline = 0;
uintptr_t g_base_vtable = 0;
uintptr_t g_vtable_storage[kTriggerSlots + 1];  // [0] = RTTI locator, then the slots
uintptr_t g_vtable = 0;                         // &g_vtable_storage[1]
uintptr_t g_original_tooltip = 0;
uintptr_t g_assign = 0;
thread_local bool t_in_tooltip = false;

const char* game_string_text(uintptr_t s) {
    return read<uint32_t>(s + 0x14) > 15 ? read<const char*>(s) : reinterpret_cast<const char*>(s);
}

// Slot 6: bool thiscall Evaluate(CEventScope*), RET 4.
bool __fastcall evaluate(uintptr_t trigger, void*, uintptr_t scope) {
    int id = read<int>(scope + kScopeProvince);
    if (id <= 0) {
        return false;
    }
    uintptr_t flags = game::province(static_cast<uint32_t>(id)) + kProvinceFlags;
    using FindFn = uintptr_t(__fastcall*)(uintptr_t, void*, const char*);
    auto find = reinterpret_cast<FindFn>(read<uintptr_t>(read<uintptr_t>(flags) + kFlagsFind * 4));
    uintptr_t entry = find(flags, nullptr, game_string_text(trigger + kTriggerFlagName));
    return entry && read<uint8_t>(entry + kFlagSet) != 0;
}

// Slot 10: thiscall (std::string* out, ?, bool negated, ?), RET 0x10; returns out.
uintptr_t __fastcall tooltip(uintptr_t trigger, void*, uintptr_t out, uint32_t a, uint32_t negated, uint32_t b) {
    using TooltipFn = uintptr_t(__fastcall*)(uintptr_t, void*, uintptr_t, uint32_t, uint32_t, uint32_t);
    t_in_tooltip = true;
    uintptr_t result = reinterpret_cast<TooltipFn>(g_original_tooltip)(trigger, nullptr, out, a, negated, b);
    t_in_tooltip = false;
    return result;
}

// args = the two stack arguments of std::string::assign: (const char* text, size_t size).
void __stdcall swap_key(uintptr_t* args) {
    if (!t_in_tooltip) {
        return;
    }
    if (args[0] == hooks::exe_va(kKeyHaveNot)) {
        args[0] = reinterpret_cast<uintptr_t>(kProvinceHaveNot);
        args[1] = sizeof(kProvinceHaveNot) - 1;
    } else if (args[0] == hooks::exe_va(kKeyHave)) {
        args[0] = reinterpret_cast<uintptr_t>(kProvinceHave);
        args[1] = sizeof(kProvinceHave) - 1;
    }
}

// Replaces the CALL std::string::assign of the tooltip's key: ECX = string, [esp + 4] = text, size.
__declspec(naked) void key_thunk() {
    __asm {
        SE_SAVE_ALL
        lea eax, [esp + SE_SAVED_ALL_BYTES + 4]
        push eax
        call swap_key
        SE_RESTORE_ALL
        jmp g_assign
    }
}

// Replaces the trigger factory: thiscall (block, parser, token), RET 8.
uintptr_t __fastcall trigger_factory(uintptr_t block, void*, uintptr_t parser, int token) {
    using FactoryFn = uintptr_t(__fastcall*)(uintptr_t, void*, uintptr_t, int);
    auto factory = reinterpret_cast<FactoryFn>(g_factory_trampoline);
    if (token != kTokenHasProvinceFlag) {
        return factory(block, nullptr, parser, token);
    }
    uintptr_t end_before = read<uintptr_t>(block + kTriggerBlockEnd);
    uintptr_t result = factory(block, nullptr, parser, kTokenHasCountryFlag);
    uintptr_t end = read<uintptr_t>(block + kTriggerBlockEnd);
    uintptr_t trigger = end && end != end_before ? read<uintptr_t>(end - 4) : 0;
    if (trigger && read<uintptr_t>(trigger) == g_base_vtable) {
        *reinterpret_cast<uintptr_t*>(trigger) = g_vtable;
    } else {
        logf("province_flags: a has_province_flag trigger could not be built");
    }
    return result;
}

}  // namespace

void install_province_flags(const Config& config) {
    if (!config.province_flags) {
        logf("province_flags: disabled");
        return;
    }
    g_base_vtable = hooks::exe_va(kHasCountryFlagVtable);
    g_original_tooltip = hooks::exe_va(kHasCountryFlagTooltip);
    if (read<uintptr_t>(g_base_vtable + kSlotTooltip * 4) != g_original_tooltip) {
        logf("province_flags: CHasCountryFlagTrigger vtable %08X not recognised, off", kHasCountryFlagVtable);
        return;
    }
    g_vtable_storage[0] = read<uintptr_t>(g_base_vtable - 4);
    for (uint32_t i = 0; i < kTriggerSlots; ++i) {
        g_vtable_storage[i + 1] = read<uintptr_t>(g_base_vtable + i * 4);
    }
    g_vtable_storage[kSlotEvaluate + 1] = reinterpret_cast<uintptr_t>(&evaluate);
    g_vtable_storage[kSlotTooltip + 1] = reinterpret_cast<uintptr_t>(&tooltip);
    g_vtable = reinterpret_cast<uintptr_t>(&g_vtable_storage[1]);

    uintptr_t factory = hooks::exe_va(kTriggerFactory);
    if (!hooks::expect_bytes(factory, kTriggerFactoryEntry, sizeof(kTriggerFactoryEntry))) {
        logf("province_flags: site %08X not recognised, off", kTriggerFactory);
        return;
    }
    g_assign = hooks::exe_va(kStringAssign);
    bool keys = true;
    for (uint32_t site : kKeyCalls) {
        keys = keys && hooks::call_target(hooks::exe_va(site)) == g_assign;
    }
    if (!(g_factory_trampoline = hooks::make_trampoline(factory, sizeof(kTriggerFactoryEntry))) ||
        !hooks::jump_hook(factory, kTriggerFactoryEntry, sizeof(kTriggerFactoryEntry),
                          reinterpret_cast<uintptr_t>(&trigger_factory))) {
        logf("province_flags: site %08X could not be hooked, off", kTriggerFactory);
        return;
    }
    if (keys) {
        for (uint32_t site : kKeyCalls) {
            hooks::redirect_call(hooks::exe_va(site), g_assign, reinterpret_cast<uintptr_t>(&key_thunk));
        }
    }
    logf("province_flags: active, has_province_flag built at the trigger factory %08X%s", kTriggerFactory,
         keys ? "" : "; tooltip sites not recognised, it reads as has_country_flag");
}

}  // namespace se::features
