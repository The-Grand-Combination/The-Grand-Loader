#pragma once

#include <cstdint>

// New script effects: docs/re/systems/effects.md ("Recipe: a new effect"). One hook on the effect factory
// (EffectBlock_AddChild 0x00876B30) serves every feature: each adds a builder for its own tokens.

namespace se::game {

// Vtable slots shared by every effect class (14 slots; the RTTI locator at [-1]).
constexpr uint32_t kEffectSlots = 14;
constexpr uint32_t kEffectSlotTooltip = 9;   // thiscall (std::string* out, scope, std::string by value, int), RET 0x28
constexpr uint32_t kEffectSlotExecute = 11;  // thiscall (scope, ?), RET 8

// A builder gets every token the factory sees; for one of its own it builds the effect (usually with
// build_template) and returns true with the factory's return value in `result`.
using EffectBuilder = bool (*)(uintptr_t block, uintptr_t parser, int token, uintptr_t& result);

// Hooks the factory on first use; false when its entry is not the expected one.
bool add_effect_builder(EffectBuilder builder);

// The vanilla factory: thiscall (block, parser, token), RET 8.
uintptr_t build_effect(uintptr_t block, uintptr_t parser, int token);

// Builds the vanilla effect `template_token` in `block` (the factory allocates, appends and parses it)
// and returns it when it has the vtable `template_vtable` (resolved), else 0. `result` = the factory's
// return value.
uintptr_t build_template(uintptr_t block, uintptr_t parser, int template_token, uintptr_t template_vtable,
                         uintptr_t& result);

// A copy of the effect vtable at `base` (resolved) in `storage` (kEffectSlots + 1 entries: [0] = the RTTI
// locator), with the tooltip and Execute replaced; returns the address to store in the effect.
uintptr_t copy_effect_vtable(uintptr_t base, uintptr_t* storage, uintptr_t tooltip, uintptr_t execute);

// Token_AddDynamic: the id of `name`, registered if new. Only once the lexer is ready (lexer_ready).
int add_token(const char* name);
bool lexer_ready();

}  // namespace se::game
