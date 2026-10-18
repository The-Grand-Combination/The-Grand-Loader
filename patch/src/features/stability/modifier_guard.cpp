#include "features/stability/modifier_guard.h"

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "game/string.h"
#include "game/world.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
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
//   Both parsers keep the effect in ESI (`MOV ESI,ECX` in the prologue, `MOV [ESI+0x24/0x20],EAX` after
//   the lookup). The registry is filled from common/event_modifiers.txt, which the game reads after
//   common/issues.txt (setup.log: issues, then modifiers), so an effect inside an issue that names an
//   event modifier gets 0 although the modifier exists.
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
constexpr uint32_t kEventModifiersPtrRva = 0xE5A69C;  // g_eventModifiers; created by the first lookup
constexpr uint32_t kAddParseCallRva = 0x481337;
constexpr uint32_t kRemoveParseCallRva = 0x48217A;
constexpr uint32_t kTooltipSlot = 0x24;
constexpr uint32_t kExecuteSlot = 0x2C;
constexpr uint32_t kLogLimit = 50;
constexpr size_t kMaxPending = 256;

struct Effect {
    const char* name;
    uint32_t vtable_rva;
    uint32_t tooltip_rva;
    uint32_t execute_rva;
    uint32_t modifier_offset;
};

constexpr Effect kEffects[] = {
    {"add_province_modifier", 0xA2ABA8, 0x4814E0, 0x481450, 0x24},
    {"add_country_modifier", 0xA2ABE0, 0x481B40, 0x481A90, 0x24},
    {"remove_province_modifier", 0xA2AC18, 0x4822C0, 0x482290, 0x20},
    {"remove_country_modifier", 0xA2AC50, 0x4826B0, 0x482680, 0x20},
};
constexpr int kEffectCount = static_cast<int>(std::size(kEffects));

uintptr_t g_find = 0;
uintptr_t g_release = 0;  // the game's operator delete
uintptr_t g_original_tooltip[kEffectCount];
uintptr_t g_original_execute[kEffectCount];
uint32_t g_logged = 0;

// An effect whose modifier was not found when it was parsed, looked up again when it is first used.
struct Pending {
    uintptr_t effect;
    std::string name;
    bool done;
};
std::vector<Pending> g_pending;

// ModifierRegistry_Find: EAX = registry, [ESP+4] = name (not consumed), RET 4.
uint32_t find_modifier(const std::string& name) {
    uint32_t registry = game::read32(hooks::exe_addr(kEventModifiersPtrRva));
    if (!registry) {
        return 0;
    }
    game::GameString view = game::view_of(name.c_str(), name.size());
    game::GameString* arg = &view;
    uintptr_t find = g_find;
    uint32_t result = 0;
    __asm {
        push ebx
        push esi
        push edi
        mov eax, registry
        push arg
        call find
        mov result, eax
        pop edi
        pop esi
        pop ebx
    }
    return result;
}

void __cdecl report_lookup(uintptr_t effect, const game::GameString* name, uint32_t result, int remove) {
    if (result != 0) {
        return;
    }
    if (g_pending.size() < kMaxPending) {
        g_pending.push_back({effect, name->c_str(), false});
    }
    if (++g_logged <= kLogLimit) {
        logf("fix_missing_modifier: %s names '%s', not loaded yet; looked up again when the effect is used",
             remove ? "remove_country_modifier / remove_province_modifier"
                    : "add_country_modifier / add_province_modifier",
             name->c_str());
    }
}

// From the guards, for an effect with no modifier: finds it now if it was defined after the effect was
// parsed and stores it in the effect (the original then runs); 0 when it really does not exist.
uint32_t __cdecl resolve_pending(uintptr_t effect, int index, int executing) {
    for (Pending& p : g_pending) {
        if (p.effect != effect || p.done) {
            continue;
        }
        p.done = true;
        uint32_t modifier = find_modifier(p.name);
        if (modifier) {
            *reinterpret_cast<uint32_t*>(effect + kEffects[index].modifier_offset) = modifier;
            logf("fix_missing_modifier: '%s' found after loading, %s works", p.name.c_str(), kEffects[index].name);
            return modifier;
        }
        logf("fix_missing_modifier: '%s' does not exist, %s does nothing", p.name.c_str(), kEffects[index].name);
        break;
    }
    if (executing && ++g_logged <= kLogLimit) {
        logf("fix_missing_modifier: %s with an unknown modifier executed, nothing done", kEffects[index].name);
    }
    return 0;
}

// In place of CALL ModifierRegistry_Find at the two parse sites: same registers in, same result out.
// After the call the stack holds EDX, ECX, the return address and the name argument; ESI is the effect.
#define CP_FIND_THUNK(name, remove)                                                       \
    __declspec(naked) void name() {                                                       \
        __asm push ecx                                                                    \
        __asm push edx                                                                    \
        __asm push dword ptr [esp + 12]                                                   \
        __asm call dword ptr [g_find]                                                     \
        CP_SAVE_ALL                                                                       \
        __asm push remove                                                                 \
        __asm push eax                                                                    \
        __asm push dword ptr [esp + CP_SAVED_ALL_BYTES + 12 + 8]                          \
        __asm push esi                                                                    \
        __asm call report_lookup                                                          \
        __asm add esp, 16                                                                 \
        CP_RESTORE_ALL                                                                    \
        __asm pop edx                                                                     \
        __asm pop ecx                                                                     \
        __asm ret 4                                                                       \
    }

CP_FIND_THUNK(find_add_thunk, 0)
CP_FIND_THUNK(find_remove_thunk, 1)
#undef CP_FIND_THUNK

// One tooltip and one execute guard per effect class: `offset` is where the class keeps its modifier.
// Tooltip with a null modifier: `out` ([esp+4]) becomes an empty string (capacity +0x14 = 15, size
// +0x10 = 0), the by-value string argument ([esp+0xC], capacity at [esp+0x20]) is freed when on the heap,
// as the game does, and `out` is returned. Execute with a null modifier: logged, and nothing else, as
// the game does silently. Both first look the modifier up again (resolve_pending); if found, the
// original runs.
#define CP_EFFECT_GUARDS(tooltip, execute, offset, index)                                 \
    __declspec(naked) void tooltip() {                                                    \
        __asm cmp dword ptr [ecx + offset], 0                                             \
        __asm jne pass                                                                    \
        CP_SAVE_ALL                                                                       \
        __asm push 0                                                                      \
        __asm push index                                                                  \
        __asm push ecx                                                                    \
        __asm call resolve_pending                                                        \
        __asm add esp, 12                                                                 \
        __asm mov [esp + CP_SAVED_EAX], eax                                               \
        CP_RESTORE_ALL                                                                    \
        __asm test eax, eax                                                               \
        __asm jnz pass                                                                    \
        __asm mov eax, [esp + 4]                                                          \
        __asm mov dword ptr [eax + 0x14], 15                                              \
        __asm mov dword ptr [eax + 0x10], 0                                               \
        __asm mov byte ptr [eax], 0                                                       \
        __asm cmp dword ptr [esp + 0x20], 16                                              \
        __asm jb done                                                                     \
        __asm push eax                                                                    \
        __asm push dword ptr [esp + 0x10]                                                 \
        __asm call dword ptr [g_release]                                                  \
        __asm add esp, 4                                                                  \
        __asm pop eax                                                                     \
        __asm done:                                                                       \
        __asm ret 0x28                                                                    \
        __asm pass:                                                                       \
        __asm jmp dword ptr [g_original_tooltip + index * 4]                              \
    }                                                                                     \
    __declspec(naked) void execute() {                                                    \
        __asm cmp dword ptr [ecx + offset], 0                                             \
        __asm jne pass                                                                    \
        CP_SAVE_ALL                                                                       \
        __asm push 1                                                                      \
        __asm push index                                                                  \
        __asm push ecx                                                                    \
        __asm call resolve_pending                                                        \
        __asm add esp, 12                                                                 \
        __asm mov [esp + CP_SAVED_EAX], eax                                               \
        CP_RESTORE_ALL                                                                    \
        __asm test eax, eax                                                               \
        __asm jnz pass                                                                    \
        __asm ret 8                                                                       \
        __asm pass:                                                                       \
        __asm jmp dword ptr [g_original_execute + index * 4]                              \
    }

static_assert(kEffects[0].modifier_offset == 0x24 && kEffects[1].modifier_offset == 0x24 &&
                  kEffects[2].modifier_offset == 0x20 && kEffects[3].modifier_offset == 0x20,
              "offsets in the guards");
CP_EFFECT_GUARDS(tooltip_add_province, execute_add_province, 0x24, 0)
CP_EFFECT_GUARDS(tooltip_add_country, execute_add_country, 0x24, 1)
CP_EFFECT_GUARDS(tooltip_remove_province, execute_remove_province, 0x20, 2)
CP_EFFECT_GUARDS(tooltip_remove_country, execute_remove_country, 0x20, 3)
#undef CP_EFFECT_GUARDS

constexpr void (*kTooltips[])() = {&tooltip_add_province, &tooltip_add_country, &tooltip_remove_province,
                                   &tooltip_remove_country};
constexpr void (*kExecutes[])() = {&execute_add_province, &execute_add_country, &execute_remove_province,
                                   &execute_remove_country};
static_assert(std::size(kTooltips) == kEffectCount && std::size(kExecutes) == kEffectCount);
static_assert(game::kStringSizeOffset == 0x10 && game::kStringCapacityOffset == 0x14, "offsets in the guards");

int install_effect_slots() {
    int slots = 0;
    for (int i = 0; i < kEffectCount; i++) {
        const Effect& e = kEffects[i];
        g_original_tooltip[i] = hooks::exe_addr(e.tooltip_rva);
        g_original_execute[i] = hooks::exe_addr(e.execute_rva);
        uintptr_t vtable = hooks::exe_addr(e.vtable_rva);
        auto tooltip = reinterpret_cast<uintptr_t>(kTooltips[i]);
        auto execute = reinterpret_cast<uintptr_t>(kExecutes[i]);
        bool tooltip_ok = hooks::replace_pointer(vtable + kTooltipSlot, g_original_tooltip[i], tooltip);
        bool execute_ok = hooks::replace_pointer(vtable + kExecuteSlot, g_original_execute[i], execute);
        if (!tooltip_ok || !execute_ok) {
            logf("fix_missing_modifier: %s %s slot not recognised, left alone", e.name,
                 tooltip_ok ? "execute" : "tooltip");
        }
        slots += (tooltip_ok ? 1 : 0) + (execute_ok ? 1 : 0);
    }
    return slots;
}

bool install_parse_site(uint32_t rva, void (*thunk)(), const char* which) {
    if (hooks::redirect_call(hooks::exe_addr(rva), g_find, reinterpret_cast<uintptr_t>(thunk))) {
        return true;
    }
    logf("fix_missing_modifier: %s parser call site %08X not recognised, names not logged", which,
         hooks::listing_va(rva));
    return false;
}

}  // namespace

void install_modifier_guard(const Config& config) {
    if (!config.fix_missing_modifier) {
        logf("fix_missing_modifier: disabled");
        return;
    }
    g_find = hooks::exe_addr(kFindRva);
    g_release = hooks::exe_addr(game::kOperatorDeleteRva);
    int slots = install_effect_slots();
    int sites = (install_parse_site(kAddParseCallRva, &find_add_thunk, "add") ? 1 : 0) +
                (install_parse_site(kRemoveParseCallRva, &find_remove_thunk, "remove") ? 1 : 0);
    logf("fix_missing_modifier: active on %d of %d effect slots and %d of 2 parser sites (unknown modifiers: "
         "looked up again on first use, else empty tooltip, no effect, logged)",
         slots, kEffectCount * 2, sites);
}

}  // namespace cp::features
