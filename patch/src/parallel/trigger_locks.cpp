#include "parallel/trigger_locks.h"

#include <windows.h>

#include <cstdint>
#include <iterator>

#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/parallel-triggers.md, "Triggers that write"):
//
//   CIsPossibleVassalTrigger (is_possible_vassal, vtable 0x00E30FC8, Evaluate 0x008D8030) and
//   CCanCreateVassalsTrigger (can_create_vassals, vtable 0x00E30F90, Evaluate 0x008D7D40) answer from
//   FUN_00519A40(country), which empties the country's vector at +0xE2C/+0xE30 and refills it (dead
//   countries with cores in its provinces whose capital it holds) on every call. Two threads evaluating
//   either for the same country resize the same vector: one frees the buffer the other writes to, and
//   the heap manager stops the game with 0xC0000374 (a user's crash dump, docs/re/log.md 2026-10-17).
//
// Every trigger class (236, from RTTI) was audited for stores into object fields along its Evaluate's
// direct calls three levels deep; these two are the only ones that write to game objects during a
// running game (the others construct temporaries or create lazy singletons at first use).
//
// Their Evaluate slot goes to one wrapper that holds a critical section around the original. The game
// state does not change during a batch, so the result is the one a serial evaluation gives; only these
// two triggers wait for each other.

namespace cp::parallel {

namespace {

constexpr uint32_t kEvaluateSlot = 0x18;

struct Locked {
    const char* keyword;
    uint32_t vtable_rva;
    uint32_t evaluate_rva;
};

constexpr Locked kLocked[] = {
    {"is_possible_vassal", 0xA30FC8, 0x4D8030},
    {"can_create_vassals", 0xA30F90, 0x4D7D40},
};
constexpr int kLockedCount = static_cast<int>(std::size(kLocked));

using EvaluateFn = char(__fastcall*)(void* self, void* edx, void* scope);

CRITICAL_SECTION g_lock;
uintptr_t g_vtables[kLockedCount];
EvaluateFn g_originals[kLockedCount];

char __fastcall locked_evaluate(void* self, void*, void* scope) {
    uintptr_t vtable = game::read32(reinterpret_cast<uintptr_t>(self));
    EvaluateFn original = g_originals[0];
    for (int i = 1; i < kLockedCount; i++) {
        if (g_vtables[i] == vtable) {
            original = g_originals[i];
        }
    }
    EnterCriticalSection(&g_lock);
    char result = original(self, nullptr, scope);
    LeaveCriticalSection(&g_lock);
    return result;
}

}  // namespace

void install_trigger_locks(const Config& config) {
    if (config.parallel_decisions == 0 && config.parallel_events == 0) {
        return;
    }
    InitializeCriticalSection(&g_lock);
    int locked = 0;
    for (int i = 0; i < kLockedCount; i++) {
        g_vtables[i] = hooks::exe_addr(kLocked[i].vtable_rva);
        uintptr_t original = hooks::exe_addr(kLocked[i].evaluate_rva);
        g_originals[i] = reinterpret_cast<EvaluateFn>(original);
        if (hooks::replace_pointer(g_vtables[i] + kEvaluateSlot, original, reinterpret_cast<uintptr_t>(&locked_evaluate))) {
            locked++;
        } else {
            logf("trigger_locks: %s Evaluate slot not recognised; parallel evaluation of it is not safe", kLocked[i].keyword);
        }
    }
    logf("trigger_locks: %d of %d triggers that write while evaluated are serialised (is_possible_vassal, "
         "can_create_vassals)",
         locked, kLockedCount);
}

}  // namespace cp::parallel
