#include "features/multiplayer/oos_log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "features/multiplayer/state_checksum.h"
#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/multiplayer.md): Update_World_and_Advance_Time compares the local sync
// vector (EAX = world+0xB74) with the one in the tick command (ECX) through Vector_Equals (0x0068F340,
// CALL at 0x00683036; returns 1 when they differ). On a difference the game writes "Checksum:" lines to
// its own log, realigns the global RNG, shows the OOS dialog once and pauses. This hook runs before the
// original call, compares the vectors itself and names what differed in the_grand_loader.log.

namespace cp::features {

namespace {

constexpr uint32_t kEqualsCallRva = 0x283036;
constexpr uint32_t kEqualsRva = 0x28F340;
constexpr int kFullLogs = 20;         // mismatching ticks logged in full
constexpr int kThenEveryDays = 30;    // afterwards one line per this many days

uintptr_t g_original_equals = 0;
int g_mismatches = 0;
int32_t g_last_logged_day = -1000000;
bool g_fpu_logged = false;

struct IntVector {
    uint32_t begin, end, capacity;
};

int32_t current_day() {
    uintptr_t world = game::world();
    return world ? static_cast<int32_t>(game::read32(world + game::kWorldDate)) / 24 : -1;
}

void log_fpu_once() {
    if (g_fpu_logged) {
        return;
    }
    g_fpu_logged = true;
    uint16_t cw = 0;
    uint32_t csr = 0;
    __asm {
        fnstcw cw
        stmxcsr csr
    }
    const char* precision = (cw & 0x300) == 0x000 ? "24-bit" : (cw & 0x300) == 0x200 ? "53-bit" : "64-bit";
    logf("oos_log: game thread x87 control word %04X (%s precision), MXCSR %08X", cw, precision, csr);
}

void __cdecl compare(const IntVector* local, const IntVector* host) {
    log_fpu_once();
    size_t n_local = (local->end - local->begin) / 4;
    size_t n_host = (host->end - host->begin) / 4;
    const uint32_t* a = reinterpret_cast<const uint32_t*>(local->begin);
    const uint32_t* b = reinterpret_cast<const uint32_t*>(host->begin);
    bool differ = n_local != n_host;
    for (size_t i = 0; !differ && i < n_local; i++) {
        differ = a[i] != b[i];
    }
    if (!differ) {
        return;
    }
    int32_t day = current_day();
    g_mismatches++;
    bool full = g_mismatches <= kFullLogs;
    if (!full && day - g_last_logged_day < kThenEveryDays) {
        return;
    }
    g_last_logged_day = day;
    if (n_local != n_host) {
        logf("oos_log: day %d: sync vector sizes differ, local %u elements, host %u: the host and this client do "
             "not run the same patch settings (state_checksum must be the same for everyone)",
             day, static_cast<unsigned>(n_local), static_cast<unsigned>(n_host));
        return;
    }
    logf("oos_log: day %d: OUT OF SYNC (mismatch #%d)", day, g_mismatches);
    if (!full) {
        return;
    }
    for (size_t i = 0; i < n_local; i++) {
        if (a[i] != b[i]) {
            logf("oos_log:   %s: local %08X, host %08X", sync_element_name(i), a[i], b[i]);
        }
    }
}

__declspec(naked) void equals_thunk() {
    __asm {
        pushad
        pushfd
        push ecx   // host (command) vector
        push eax   // local (world) vector
        call compare
        add esp, 8
        popfd
        popad
        jmp dword ptr [g_original_equals]
    }
}

}  // namespace

void install_oos_log(const Config& config) {
    if (!config.oos_log) {
        logf("oos_log: disabled");
        return;
    }
    uintptr_t site = hooks::exe_addr(kEqualsCallRva);
    if (*reinterpret_cast<const unsigned char*>(site) != 0xE8) {
        logf("oos_log: no CALL at %08X, feature off", kEqualsCallRva + 0x400000);
        return;
    }
    int32_t rel;
    std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
    uintptr_t current = site + 5 + static_cast<uintptr_t>(rel);
    if (current != hooks::exe_addr(kEqualsRva)) {
        logf("oos_log: the Vector_Equals call site is already redirected; chaining to it");
    }
    g_original_equals = current;
    if (!hooks::redirect_call(site, current, reinterpret_cast<uintptr_t>(&equals_thunk))) {
        logf("oos_log: could not patch the compare call site, feature off");
        return;
    }
    logf("oos_log: active");
}

}  // namespace cp::features
