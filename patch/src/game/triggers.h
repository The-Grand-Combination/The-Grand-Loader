#pragma once

#include <cstddef>
#include <cstdint>

#include "game/world.h"

// Trigger blocks and event scopes of v2game.exe 3.04 (docs/re/systems/triggers.md), shared by the
// features that classify or evaluate triggers (cb_cull, decision_cull, events_parallel).

namespace cp::game {

constexpr uint32_t kAndTriggerVtableRva = 0x9F9994;  // CAndTrigger::vftable (also every embedded block)
constexpr uint32_t kOrTriggerVtableRva = 0xA2FB70;   // COrTrigger::vftable
constexpr uint32_t kNotTriggerVtableRva = 0xA2FBA8;  // CNotTrigger::vftable
constexpr uint32_t kScopeCtorRva = 0x4A8650;         // CEventScope::ctor(this, parent)

// Trigger block (CAndTrigger and the other containers): vector<CTrigger*> of its children.
constexpr uint32_t kTriggerBlockBegin = 0x08;
constexpr uint32_t kTriggerBlockEnd = 0x0C;
// CTrigger
constexpr uint32_t kTriggerEvaluateSlot = 0x18;  // vtable slot 6: bool Evaluate(scope), thiscall
constexpr uint32_t kTriggerThisFlag = 0x18;      // u8: the value was `THIS`
constexpr uint32_t kTriggerFromFlag = 0x19;      // u8: the value was `FROM`

// CEventScope
constexpr uint32_t kScopeTag = 0x08;       // country tag
constexpr uint32_t kScopeIndex = 0x0C;     // country index
constexpr uint32_t kScopeProvince = 0x10;  // province index
constexpr uint32_t kScopeThis = 0x24;      // THIS scope pointer; the constructor points it at the scope itself
constexpr size_t kScopeBytes = 0x48;       // what the constructor writes and EventScope_Copy copies
constexpr size_t kScopeBuffer = 0x60;      // room the features allocate for one

// __thiscall in the game; __fastcall takes `this` in ECX and ignores EDX.
using EvaluateFn = char(__fastcall*)(void* self, void* edx, void* scope);
using ScopeCtorFn = void*(__fastcall*)(void* self, void* edx, void* parent);

inline EvaluateFn evaluate_slot(uintptr_t trigger) {
    return reinterpret_cast<EvaluateFn>(read32(read32(trigger) + kTriggerEvaluateSlot));
}

inline bool evaluate(uintptr_t trigger, void* scope) {
    return evaluate_slot(trigger)(reinterpret_cast<void*>(trigger), nullptr, scope) != 0;
}

}  // namespace cp::game
