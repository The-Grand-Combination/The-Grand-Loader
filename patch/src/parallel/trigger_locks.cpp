#include "parallel/trigger_locks.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
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
//   the heap manager stops the game with 0xC0000374 (docs/re/log.md, 2026-10-17).
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
LONG g_unknown_logged = 0;

char __fastcall locked_evaluate(void* self, void*, void* scope);

// The original Evaluate for `self`. The wrapper is installed in the two vtables above only, so an
// object of any other vtable reaching it means a copied vtable (another feature cloning one of these
// tables after install, as parallel_events does for CAndTrigger). Then the object's own slot is used
// when it is not this wrapper (the function that object really dispatches to; calling it under the
// lock is harmless); when the copy carries the wrapper itself, calling it would recurse, so the last
// entry's original stands in, as before, and the case is logged once.
EvaluateFn original_for(void* self) {
    uintptr_t vtable = game::read32(reinterpret_cast<uintptr_t>(self));
    for (int i = 0; i < kLockedCount; i++) {
        if (g_vtables[i] == vtable) {
            return g_originals[i];
        }
    }
    uintptr_t slot = game::read32(vtable + kEvaluateSlot);
    EvaluateFn fallback = slot != reinterpret_cast<uintptr_t>(&locked_evaluate) ? reinterpret_cast<EvaluateFn>(slot)
                                                                                : g_originals[kLockedCount - 1];
    if (InterlockedCompareExchange(&g_unknown_logged, 1, 0) == 0) {
        logf("trigger_locks: Evaluate reached from an unknown vtable %08X (slot holds %08X); using %s",
             hooks::listing_va_of(vtable), hooks::listing_va_of(slot),
             slot != reinterpret_cast<uintptr_t>(&locked_evaluate) ? "that slot" : kLocked[kLockedCount - 1].keyword);
    }
    return fallback;
}

char __fastcall locked_evaluate(void* self, void*, void* scope) {
    EvaluateFn original = original_for(self);
    EnterCriticalSection(&g_lock);
    char result = original(self, nullptr, scope);
    LeaveCriticalSection(&g_lock);
    return result;
}

}  // namespace

void install_trigger_locks(const Config& config) {
    if (config.parallel_decisions == 0 && config.parallel_events == 0) {
        logf("trigger_locks: off (no parallel feature on)");
        return;
    }
    InitializeCriticalSection(&g_lock);
    int locked = 0;
    for (int i = 0; i < kLockedCount; i++) {
        g_vtables[i] = hooks::exe_addr(kLocked[i].vtable_rva);
        uintptr_t original = hooks::exe_addr(kLocked[i].evaluate_rva);
        g_originals[i] = reinterpret_cast<EvaluateFn>(original);
        auto wrapper = reinterpret_cast<uintptr_t>(&locked_evaluate);
        if (hooks::replace_pointer(g_vtables[i] + kEvaluateSlot, original, wrapper)) {
            locked++;
        } else {
            logf("trigger_locks: %s Evaluate slot %08X not recognised (expected %08X, holds %08X); parallel "
                 "evaluation of it is not safe",
                 kLocked[i].keyword, hooks::listing_va(kLocked[i].vtable_rva + kEvaluateSlot),
                 hooks::listing_va(kLocked[i].evaluate_rva),
                 hooks::listing_va_of(game::read32(g_vtables[i] + kEvaluateSlot)));
        }
    }
    char names[96] = "";
    for (const Locked& l : kLocked) {
        size_t used = std::strlen(names);
        std::snprintf(names + used, sizeof(names) - used, "%s%s", used ? ", " : "", l.keyword);
    }
    logf("trigger_locks: %d of %d triggers that write while evaluated are serialised (%s)", locked, kLockedCount,
         names);
}

}  // namespace cp::parallel
