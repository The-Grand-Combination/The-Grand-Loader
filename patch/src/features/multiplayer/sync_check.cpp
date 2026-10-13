#include "features/multiplayer/sync_check.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "features/multiplayer/oos_log.h"
#include "features/multiplayer/state_checksum.h"
#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04, Update_World_and_Advance_Time (docs/re/systems/multiplayer.md):
//
//   0x00683036  CALL Vector_Equals        EAX = &world->syncVector (+0xB74), ECX = &command->vector (+0x3C);
//                                         returns 1 when sizes or any element differ
//   ... mismatch path: log, g_rngCalls = host[0], g_mtSeed = host[1], g_rngCalls = 0,
//   0x0068364D  CALL Rng_Resync           reseeds g_mtState from g_mtSeed and advances g_rngCalls draws
//   ... OOS dialog once, CPauseGame
//
// Measured on a two-instance game (2026-10-13): after the first mismatch the client's draw count is 0
// at every tick and its state diverges completely within a day, because the realign restarts the
// generator from the seed. The host's position when the client compares is the host's count after its
// DayTick of the previous day, which state_checksum sends as `rng_calls_end`.

namespace cp::features {

namespace {

constexpr uint32_t kEqualsCallRva = 0x283036;
constexpr uint32_t kEqualsRva = 0x28F340;
constexpr uint32_t kResyncCallRva = 0x28364D;
constexpr uint32_t kResyncRva = 0x5B7C80;
constexpr uint32_t kRngCallsRva = 0xB20C14;  // g_rngCalls
constexpr size_t kGameElements = 2;

struct IntVector {
    uint32_t begin, end, capacity;
};

uintptr_t g_original_equals = 0;
uintptr_t g_original_resync = 0;
bool g_fix_resync = false;
uint32_t g_host_copy[64];
size_t g_host_size = 0;
IntVector* g_local = nullptr;
IntVector* g_host = nullptr;
uint32_t g_saved_local_end = 0;
uint32_t g_saved_host_end = 0;

// Before the game's compare: keep a copy of the host vector for the resync and the report, then
// shorten both vectors to the two game elements so that Vector_Equals only sees those.
void __cdecl before_compare(IntVector* local, IntVector* host) {
    g_local = local;
    g_host = host;
    size_t n_host = (host->end - host->begin) / 4;
    g_host_size = n_host < 64 ? n_host : 64;
    std::memcpy(g_host_copy, reinterpret_cast<const void*>(host->begin), g_host_size * 4);
    report_sync_vectors(reinterpret_cast<const uint32_t*>(local->begin), (local->end - local->begin) / 4,
                        g_host_copy, g_host_size);
    g_saved_local_end = local->end;
    g_saved_host_end = host->end;
    size_t n_local = (local->end - local->begin) / 4;
    if (n_local >= kGameElements && n_host >= kGameElements) {
        local->end = local->begin + kGameElements * 4;
        host->end = host->begin + kGameElements * 4;
    }
}

void __cdecl after_compare() {
    if (g_local) {
        g_local->end = g_saved_local_end;
        g_host->end = g_saved_host_end;
    }
}

__declspec(naked) void equals_thunk() {
    __asm {
        pushad
        pushfd
        push ecx
        push eax
        call before_compare
        add esp, 8
        popfd
        popad
        call dword ptr [g_original_equals]
        push eax
        pushad
        pushfd
        call after_compare
        popfd
        popad
        pop eax
        ret
    }
}

// In the mismatch path, right before Rng_Resync: the game has just set g_rngCalls = 0. Replace it with
// the host's position: `rng_calls_end` when the host runs state_checksum, else the count it sent
// (one day behind, but not zero).
void __cdecl before_resync() {
    uint32_t* calls = reinterpret_cast<uint32_t*>(hooks::exe_addr(kRngCallsRva));
    uint32_t target = 0;
    const char* source = "nothing (vanilla: restart from the seed)";
    size_t end_index = sync_element_index("rng_calls_end");
    if (g_host_size > end_index && end_index != static_cast<size_t>(-1)) {
        target = g_host_copy[end_index];
        source = "host rng_calls_end";
    } else if (g_host_size > 0) {
        target = g_host_copy[0];
        source = "host rng_calls at its day start (no rng_calls_end in its vector)";
    }
    logf("fix_rng_resync: realigning the global RNG to %u draws from the seed (%s)", target, source);
    *calls = target;
}

__declspec(naked) void resync_thunk() {
    __asm {
        pushad
        pushfd
        call before_resync
        popfd
        popad
        jmp dword ptr [g_original_resync]
    }
}

bool hook_call(uint32_t site_rva, uint32_t expected_rva, uintptr_t& original, void* thunk, const char* what) {
    uintptr_t site = hooks::exe_addr(site_rva);
    if (*reinterpret_cast<const unsigned char*>(site) != 0xE8) {
        logf("sync_check: no CALL at %08X (%s), off", site_rva + 0x400000, what);
        return false;
    }
    int32_t rel;
    std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
    uintptr_t current = site + 5 + static_cast<uintptr_t>(rel);
    if (current != hooks::exe_addr(expected_rva)) {
        logf("sync_check: the %s call site is already redirected; chaining to it", what);
    }
    original = current;
    if (!hooks::redirect_call(site, current, reinterpret_cast<uintptr_t>(thunk))) {
        logf("sync_check: could not patch the %s call site, off", what);
        return false;
    }
    return true;
}

}  // namespace

SyncVectorView last_host_vector() { return {g_host_copy, g_host_size}; }

void install_sync_check(const Config& config) {
    bool want_compare = config.state_checksum || config.oos_log;
    if (!want_compare && !config.fix_rng_resync) {
        logf("sync_check: nothing enabled");
        return;
    }
    if (want_compare && hook_call(kEqualsCallRva, kEqualsRva, g_original_equals, &equals_thunk, "Vector_Equals")) {
        logf("sync_check: the game compares its two elements; the rest is compared and logged here");
    }
    if (config.fix_rng_resync && hook_call(kResyncCallRva, kResyncRva, g_original_resync, &resync_thunk, "Rng_Resync")) {
        g_fix_resync = true;
        logf("fix_rng_resync: active, a sync mismatch realigns the RNG to the host's position instead of the seed");
    } else if (!config.fix_rng_resync) {
        logf("fix_rng_resync: disabled");
    }
}

}  // namespace cp::features
