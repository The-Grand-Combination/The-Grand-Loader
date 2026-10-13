#include "features/multiplayer/host_wait.h"

#include <windows.h>

#include <cstdint>

#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/multiplayer.md):
//
//   CInGameIdler_Update: CALL ProcessGameTimeAdvance (0x00682BD0, __fastcall(world), frame delta in XMM0)
//                        at 0x0065571C. On the host it issues the next CHourlyTickCommand when the
//                        speed accumulator passed its threshold and the AI buckets are empty.
//   Every tick each machine sends a CClientPingCommand with its date; on the host, its Execute
//   (0x0072F1E0) stores it in that player's entry (+0x34) of the player list at world+0x18 (entries:
//   +0x1C id, -1 = unused; +0x34 date in hours; +0x3C next). It warns and lowers the speed at 6 days
//   of lag, pauses at 11. The "soft" reaction above 2 days (g_timeFactor) only changes how often the
//   host renders inside a tick; it does not slow the ticks.
//
// This feature skips ProcessGameTimeAdvance on the host while any player is more than N days behind
// the host's date, so the host waits for the slowest player instead of racing ahead. A player whose
// pings stop (disconnect) would hold the host forever, so the wait is released after a timeout.

namespace cp::features {

namespace {

constexpr uint32_t kCallSiteRva = 0x25571C;
constexpr uint32_t kAdvanceRva = 0x282BD0;
constexpr uint32_t kInGameIdlerVtableRva = 0xA0A34C;
constexpr uint32_t kIdlerSessionManagerSlot = 0x44;
constexpr uint32_t kSessionManagerIsServer = 0x34;
constexpr uint32_t kWorldPlayers = 0x18;
constexpr uint32_t kPlayerId = 0x1C;
constexpr uint32_t kPlayerDate = 0x34;
constexpr uint32_t kPlayerNext = 0x3C;
constexpr uint32_t kReleaseAfterMs = 15000;

uintptr_t g_original = 0;
int g_max_days = 0;
bool g_waiting = false;
uint32_t g_wait_started = 0;
bool g_timed_out = false;
uint32_t g_episodes = 0, g_frames_waited = 0;

bool is_server(uintptr_t world) {
    uintptr_t idler = game::read32(world + 0xB24);
    if (!idler || game::read32(idler) != hooks::exe_addr(kInGameIdlerVtableRva)) {
        return false;
    }
    using GetFn = uintptr_t(__thiscall*)(uintptr_t);
    uintptr_t mgr = reinterpret_cast<GetFn>(game::read32(game::read32(idler) + kIdlerSessionManagerSlot))(idler);
    return mgr && *reinterpret_cast<const uint8_t*>(mgr + kSessionManagerIsServer) != 0;
}

// Days the slowest player is behind the host's date, by the last ping received from each.
int max_lag_days(uintptr_t world) {
    int32_t now = static_cast<int32_t>(game::read32(world + game::kWorldDate)) / 24;
    int worst = 0;
    for (uintptr_t p = game::read32(world + kWorldPlayers); p; p = game::read32(p + kPlayerNext)) {
        if (static_cast<int32_t>(game::read32(p + kPlayerId)) == -1) {
            continue;
        }
        uint32_t date = game::read32(p + kPlayerDate);
        if (date == 0) {
            continue;  // never reported
        }
        int lag = now - static_cast<int32_t>(date) / 24;
        if (lag > worst) {
            worst = lag;
        }
    }
    return worst;
}

// Returns non-zero when the time advance should be skipped this frame.
int __cdecl should_wait(uintptr_t world) {
    if (!world || !is_server(world)) {
        return 0;
    }
    int lag = max_lag_days(world);
    uint32_t now = GetTickCount();
    if (lag <= g_max_days) {
        if (g_waiting) {
            g_waiting = false;
            g_timed_out = false;
        }
        return 0;
    }
    if (!g_waiting) {
        g_waiting = true;
        g_wait_started = now;
        g_episodes++;
        if (g_episodes <= 10) {
            logf("mp_host_wait: a player is %d day(s) behind, holding the next tick", lag);
        }
    } else if (now - g_wait_started > kReleaseAfterMs) {
        if (!g_timed_out) {
            g_timed_out = true;
            logf("mp_host_wait: still %d day(s) behind after %u ms without catching up, releasing (disconnected player?)",
                 lag, kReleaseAfterMs);
        }
        return 0;
    }
    g_frames_waited++;
    return 1;
}

// ProcessGameTimeAdvance is __fastcall with the world in ECX and the frame delta in XMM0; nothing on
// the stack, so skipping it is a plain return.
__declspec(naked) void advance_thunk() {
    __asm {
        pushad
        pushfd
        sub esp, 16
        movdqu [esp], xmm0
        push ecx
        call should_wait
        add esp, 4
        mov [esp + 48], eax          // saved eax of pushad: 16 (xmm0) + 4 (eflags) + 28 (edi..ecx)
        movdqu xmm0, [esp]
        add esp, 16
        popfd
        popad
        test eax, eax
        jnz skip
        jmp dword ptr [g_original]
    skip:
        ret
    }
}

}  // namespace

void install_host_wait(const Config& config) {
    if (config.mp_host_wait_days <= 0) {
        logf("mp_host_wait: disabled");
        return;
    }
    g_max_days = config.mp_host_wait_days;
    g_original = hooks::exe_addr(kAdvanceRva);
    if (!hooks::redirect_call(hooks::exe_addr(kCallSiteRva), g_original, reinterpret_cast<uintptr_t>(&advance_thunk))) {
        logf("mp_host_wait: call site %08X not recognised, off", kCallSiteRva + 0x400000);
        return;
    }
    logf("mp_host_wait: active, the host holds its ticks while a player is more than %d day(s) behind", g_max_days);
}

}  // namespace cp::features
