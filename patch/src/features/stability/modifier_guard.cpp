#include "features/stability/modifier_guard.h"

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04. Four effect classes share two parse functions and one lookup:
//
//   ModifierRegistry_Find (0x005661B0): EAX = registry (g_eventModifiers 0x0125A69C), [ESP+4] = the
//   name as a std::string*, returns the modifier or 0, RET 4.
//     called at 0x00881337 by the add parser (FUN_00881260, stores the result at effect+0x24 and
//     logs "Unknown modifier-type" to the game's log) and at 0x0088217A by the remove parser
//     (FUN_008820D0, effect+0x20, same log).
//
//   vtable               class                           +0x24 tooltip   +0x2C execute   modifier at
//   0x00E2ABA8           CAddProvinceModifierEffect      0x008814E0      0x00881450      +0x24
//   0x00E2ABE0           CAddCountryModifierEffect       0x00881B40      0x00881A90      +0x24
//   0x00E2AC18           CRemoveProvinceModifierEffect   0x008822C0      0x00882290      +0x20
//   0x00E2AC50           CRemoveCountryModifierEffect    0x008826B0      0x00882680      +0x20
//
//   execute: __thiscall(effect, scope, arg), RET 8. The add executes return when the modifier is
//   null; the remove executes search the lists for it and find nothing. No crash there.
//   tooltip: __thiscall(effect, out, scope, std::string by value at [ESP+0xC..0x27], arg), RET 0x28,
//   returns `out` in EAX. Each calls modifier->vt[0x18] (its name) and crashes on null. Their own
//   "nothing to say" path (0x00882320) initialises `out` empty, frees the by-value string's heap
//   buffer when its capacity is 16 or more, and returns; the guard does the same.

namespace cp::features {

namespace {

constexpr uint32_t kFindRva = 0x1661B0;
constexpr uint32_t kAddParseCallRva = 0x481337;
constexpr uint32_t kRemoveParseCallRva = 0x48217A;
constexpr uint32_t kOperatorDeleteRva = 0x6AE91B;

struct Effect {
    const char* name;
    uint32_t vtable_rva;
    uint32_t tooltip_rva;
    uint32_t execute_rva;
};

constexpr Effect kEffects[] = {
    {"add_province_modifier", 0xA2ABA8, 0x4814E0, 0x481450},
    {"add_country_modifier", 0xA2ABE0, 0x481B40, 0x481A90},
    {"remove_province_modifier", 0xA2AC18, 0x4822C0, 0x482290},
    {"remove_country_modifier", 0xA2AC50, 0x4826B0, 0x482680},
};
constexpr uint32_t kTooltipSlot = 0x24;
constexpr uint32_t kExecuteSlot = 0x2C;
constexpr uint32_t kLogLimit = 50;

// MSVC 2010 std::string.
struct GameString {
    union {
        char buf[16];
        const char* ptr;
    };
    uint32_t size;
    uint32_t capacity;
    const char* c_str() const { return capacity >= 16 ? ptr : buf; }
};

uintptr_t g_find = 0;
uintptr_t g_operator_delete = 0;
uintptr_t g_original_tooltip[4];
uintptr_t g_original_execute[4];
uint32_t g_logged = 0;

void __cdecl report_lookup(const GameString* name, uint32_t result, int remove) {
    if (result != 0 || ++g_logged > kLogLimit) {
        return;
    }
    logf("fix_missing_modifier: %s refers to the unknown modifier '%s'; the effect is disabled",
         remove ? "remove_country_modifier / remove_province_modifier" : "add_country_modifier / add_province_modifier",
         name->c_str());
}

void __cdecl report_skipped(int effect) {
    if (++g_logged > kLogLimit) {
        return;
    }
    logf("fix_missing_modifier: %s with an unknown modifier executed, nothing done", kEffects[effect].name);
}

// In place of CALL ModifierRegistry_Find at the two parse sites: same registers in, same result out.
__declspec(naked) void find_add_thunk() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        call dword ptr [g_find]
        push eax
        push 0
        push eax
        push dword ptr [esp + 24]
        call report_lookup
        add esp, 12
        pop eax
        pop edx
        pop ecx
        ret 4
    }
}

__declspec(naked) void find_remove_thunk() {
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        call dword ptr [g_find]
        push eax
        push 1
        push eax
        push dword ptr [esp + 24]
        call report_lookup
        add esp, 12
        pop eax
        pop edx
        pop ecx
        ret 4
    }
}

// Tooltip with a null modifier: `out` becomes an empty string, the by-value string argument is
// destroyed as the game does, `out` is returned.
#define CP_EMPTY_TOOLTIP                       \
    __asm mov eax, [esp + 4]                   \
    __asm mov dword ptr [eax + 0x14], 15       \
    __asm mov dword ptr [eax + 0x10], 0        \
    __asm mov byte ptr [eax], 0                \
    __asm cmp dword ptr [esp + 0x20], 16       \
    __asm jb done                              \
    __asm push eax                             \
    __asm push dword ptr [esp + 0x10]          \
    __asm call dword ptr [g_operator_delete]   \
    __asm add esp, 4                           \
    __asm pop eax                              \
    __asm done:                                \
    __asm ret 0x28

__declspec(naked) void tooltip_add_province() {
    __asm {
        cmp dword ptr [ecx + 0x24], 0
        jne pass
    }
    CP_EMPTY_TOOLTIP
    __asm {
    pass:
        jmp dword ptr [g_original_tooltip]
    }
}

__declspec(naked) void tooltip_add_country() {
    __asm {
        cmp dword ptr [ecx + 0x24], 0
        jne pass
    }
    CP_EMPTY_TOOLTIP
    __asm {
    pass:
        jmp dword ptr [g_original_tooltip + 4]
    }
}

__declspec(naked) void tooltip_remove_province() {
    __asm {
        cmp dword ptr [ecx + 0x20], 0
        jne pass
    }
    CP_EMPTY_TOOLTIP
    __asm {
    pass:
        jmp dword ptr [g_original_tooltip + 8]
    }
}

__declspec(naked) void tooltip_remove_country() {
    __asm {
        cmp dword ptr [ecx + 0x20], 0
        jne pass
    }
    CP_EMPTY_TOOLTIP
    __asm {
    pass:
        jmp dword ptr [g_original_tooltip + 12]
    }
}

#undef CP_EMPTY_TOOLTIP

// Execute with a null modifier: log and return (what the game does, silently).
#define CP_SKIP_EXECUTE(index)                 \
    __asm push ecx                             \
    __asm push edx                             \
    __asm push index                           \
    __asm call report_skipped                  \
    __asm add esp, 4                           \
    __asm pop edx                              \
    __asm pop ecx                              \
    __asm ret 8

__declspec(naked) void execute_add_province() {
    __asm {
        cmp dword ptr [ecx + 0x24], 0
        jne pass
    }
    CP_SKIP_EXECUTE(0)
    __asm {
    pass:
        jmp dword ptr [g_original_execute]
    }
}

__declspec(naked) void execute_add_country() {
    __asm {
        cmp dword ptr [ecx + 0x24], 0
        jne pass
    }
    CP_SKIP_EXECUTE(1)
    __asm {
    pass:
        jmp dword ptr [g_original_execute + 4]
    }
}

__declspec(naked) void execute_remove_province() {
    __asm {
        cmp dword ptr [ecx + 0x20], 0
        jne pass
    }
    CP_SKIP_EXECUTE(2)
    __asm {
    pass:
        jmp dword ptr [g_original_execute + 8]
    }
}

__declspec(naked) void execute_remove_country() {
    __asm {
        cmp dword ptr [ecx + 0x20], 0
        jne pass
    }
    CP_SKIP_EXECUTE(3)
    __asm {
    pass:
        jmp dword ptr [g_original_execute + 12]
    }
}

#undef CP_SKIP_EXECUTE

constexpr void (*kTooltips[])() = {&tooltip_add_province, &tooltip_add_country, &tooltip_remove_province,
                                   &tooltip_remove_country};
constexpr void (*kExecutes[])() = {&execute_add_province, &execute_add_country, &execute_remove_province,
                                   &execute_remove_country};

}  // namespace

void install_modifier_guard(const Config& config) {
    if (!config.fix_missing_modifier) {
        logf("fix_missing_modifier: disabled");
        return;
    }
    g_find = hooks::exe_addr(kFindRva);
    g_operator_delete = hooks::exe_addr(kOperatorDeleteRva);
    int slots = 0;
    for (int i = 0; i < 4; i++) {
        const Effect& e = kEffects[i];
        g_original_tooltip[i] = hooks::exe_addr(e.tooltip_rva);
        g_original_execute[i] = hooks::exe_addr(e.execute_rva);
        uintptr_t vtable = hooks::exe_addr(e.vtable_rva);
        if (hooks::replace_pointer(vtable + kTooltipSlot, g_original_tooltip[i], reinterpret_cast<uintptr_t>(kTooltips[i]))) {
            slots++;
        } else {
            logf("fix_missing_modifier: %s tooltip slot not recognised, left alone", e.name);
        }
        if (hooks::replace_pointer(vtable + kExecuteSlot, g_original_execute[i], reinterpret_cast<uintptr_t>(kExecutes[i]))) {
            slots++;
        } else {
            logf("fix_missing_modifier: %s execute slot not recognised, left alone", e.name);
        }
    }
    int sites = 0;
    if (hooks::redirect_call(hooks::exe_addr(kAddParseCallRva), g_find, reinterpret_cast<uintptr_t>(&find_add_thunk))) {
        sites++;
    } else {
        logf("fix_missing_modifier: add parser call site %08X not recognised, names not logged", kAddParseCallRva + 0x400000);
    }
    if (hooks::redirect_call(hooks::exe_addr(kRemoveParseCallRva), g_find, reinterpret_cast<uintptr_t>(&find_remove_thunk))) {
        sites++;
    } else {
        logf("fix_missing_modifier: remove parser call site %08X not recognised, names not logged", kRemoveParseCallRva + 0x400000);
    }
    logf("fix_missing_modifier: active on %d of 8 effect slots and %d of 2 parser sites (unknown modifiers: empty tooltip, no effect, logged)",
         slots, sites);
}

}  // namespace cp::features
