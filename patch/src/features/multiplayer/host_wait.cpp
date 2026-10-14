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
//   (0x0072F1E0) stores it in that player's entry of the list at world+0x18 and warns and lowers the
//   speed at 6 days of lag, pauses at 11. The "soft" reaction above 2 days (g_timeFactor) only changes
//   how often the host renders inside a tick; it does not slow the ticks.
//
// This feature skips ProcessGameTimeAdvance on the host while any player is more than N days behind
// the host's date, so the host waits for the slowest player instead of racing ahead. A player whose
// reported date stops changing (disconnect) is ignored after a timeout, so it cannot hold the host.

namespace cp::features {

namespace {

constexpr uint32_t kCallSiteRva = 0x25571C;
constexpr uint32_t kAdvanceRva = 0x282BD0;
constexpr uint32_t kStaleAfterMs = 15000;
constexpr int kMaxTracked = 32;

struct Tracked {
    int32_t id;
    uint32_t date;
    uint32_t changed_at;  // GetTickCount when `date` last changed
};

uintptr_t g_original = 0;
int g_max_days = 0;
Tracked g_tracked[kMaxTracked];
int g_tracked_count = 0;
bool g_waiting = false;
uint32_t g_episodes = 0;

Tracked& track(int32_t id, uint32_t date, uint32_t now) {
    for (int i = 0; i < g_tracked_count; i++) {
        Tracked& t = g_tracked[i];
        if (t.id == id) {
            if (t.date != date) {
                t.date = date;
                t.changed_at = now;
            }
            return t;
        }
    }
    if (g_tracked_count == kMaxTracked) {
        g_tracked_count = 0;  // more players than slots: start over
    }
    Tracked& t = g_tracked[g_tracked_count++];
    t = {id, date, now};
    return t;
}

// Days the slowest player is behind the host's date, by the last date each one reported. A player
// whose date has not moved for kStaleAfterMs is not counted.
int max_lag_days(uintptr_t world, uint32_t now) {
    int32_t today = game::day(world);
    int worst = 0;
    for (uintptr_t p = game::first_player(world); p; p = game::next_player(p)) {
        int32_t id = game::player_id(p);
        uint32_t date = game::player_date(p);
        if (id == -1 || date == 0) {
            continue;
        }
        const Tracked& t = track(id, date, now);
        if (now - t.changed_at > kStaleAfterMs) {
            continue;
        }
        int lag = today - static_cast<int32_t>(date) / 24;
        if (lag > worst) {
            worst = lag;
        }
    }
    return worst;
}

// Non-zero when the time advance should be skipped this frame.
int __cdecl should_wait(uintptr_t world) {
    uintptr_t mgr = game::session_manager(world);
    if (!mgr || *reinterpret_cast<const uint8_t*>(mgr + game::kSessionManagerIsServer) == 0) {
        return 0;
    }
    int lag = max_lag_days(world, GetTickCount());
    if (lag <= g_max_days) {
        g_waiting = false;
        return 0;
    }
    if (!g_waiting) {
        g_waiting = true;
        if (++g_episodes <= 10) {
            logf("mp_host_wait: a player is %d day(s) behind, holding the next tick", lag);
        }
    }
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
