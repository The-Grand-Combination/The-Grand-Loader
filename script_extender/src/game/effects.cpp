#include "game/effects.h"

#include <cstdint>
#include <string>

#include "game/objects.h"
#include "hooks/memory.h"
#include "log.h"

namespace se::game {

namespace {

constexpr uint32_t kEffectFactory = 0x00876B30;  // EffectBlock_AddChild: effect slot 4, thiscall (block, parser, token), RET 8
constexpr uint8_t kEffectFactoryEntry[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr uint32_t kEffectListTail = 0x0C;          // block: list of children {effect, prev, next}; +0x08 head
constexpr uint32_t kTokenAddDynamic = 0x009902D0;   // cdecl (const std::string* name, bool allowExisting) -> id
constexpr uint32_t kLexerReady = 0x01258A64;        // u8
constexpr size_t kMaxBuilders = 8;

using FactoryFn = uintptr_t(__fastcall*)(uintptr_t, void*, uintptr_t, int);

uintptr_t g_trampoline = 0;
bool g_failed = false;
EffectBuilder g_builders[kMaxBuilders];
size_t g_builder_count = 0;

uintptr_t __fastcall factory(uintptr_t block, void*, uintptr_t parser, int token) {
    uintptr_t result = 0;
    for (size_t i = 0; i < g_builder_count; ++i) {
        if (g_builders[i](block, parser, token, result)) {
            return result;
        }
    }
    return reinterpret_cast<FactoryFn>(g_trampoline)(block, nullptr, parser, token);
}

}  // namespace

bool add_effect_builder(EffectBuilder builder) {
    if (g_failed || g_builder_count == kMaxBuilders) {
        return false;
    }
    if (!g_trampoline) {
        uintptr_t site = hooks::exe_va(kEffectFactory);
        uintptr_t trampoline = 0;
        if (!hooks::expect_bytes(site, kEffectFactoryEntry, sizeof(kEffectFactoryEntry)) ||
            !(trampoline = hooks::make_trampoline(site, sizeof(kEffectFactoryEntry)))) {
            g_failed = true;
            logf("effects: the effect factory at %08X is not the expected one; no new effects", kEffectFactory);
            return false;
        }
        g_trampoline = trampoline;  // before the jump: the factory may run on another thread at once
        if (!hooks::jump_hook(site, kEffectFactoryEntry, sizeof(kEffectFactoryEntry), reinterpret_cast<uintptr_t>(&factory))) {
            g_failed = true;
            logf("effects: the effect factory at %08X could not be hooked; no new effects", kEffectFactory);
            return false;
        }
    }
    g_builders[g_builder_count++] = builder;
    return true;
}

uintptr_t build_effect(uintptr_t block, uintptr_t parser, int token) {
    return reinterpret_cast<FactoryFn>(g_trampoline)(block, nullptr, parser, token);
}

uintptr_t build_template(uintptr_t block, uintptr_t parser, int template_token, uintptr_t template_vtable,
                         uintptr_t& result) {
    uintptr_t tail_before = read<uintptr_t>(block + kEffectListTail);
    result = build_effect(block, parser, template_token);
    uintptr_t tail = read<uintptr_t>(block + kEffectListTail);
    uintptr_t effect = tail && tail != tail_before ? read<uintptr_t>(tail) : 0;
    return effect && read<uintptr_t>(effect) == template_vtable ? effect : 0;
}

uintptr_t copy_effect_vtable(uintptr_t base, uintptr_t* storage, uintptr_t tooltip, uintptr_t execute) {
    storage[0] = read<uintptr_t>(base - 4);
    for (uint32_t i = 0; i < kEffectSlots; ++i) {
        storage[i + 1] = read<uintptr_t>(base + i * 4);
    }
    storage[kEffectSlotTooltip + 1] = tooltip;
    storage[kEffectSlotExecute + 1] = execute;
    return reinterpret_cast<uintptr_t>(&storage[1]);
}

int add_token(const char* name) {
    std::string s = name;
    GameString game_name(s);
    return reinterpret_cast<int(__cdecl*)(const GameString*, bool)>(hooks::exe_va(kTokenAddDynamic))(&game_name, true);
}

bool lexer_ready() { return read<uint8_t>(hooks::exe_va(kLexerReady)) != 0; }

}  // namespace se::game
