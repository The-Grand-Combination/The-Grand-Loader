#include "features/multiplayer/world_rng.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/multiplayer.md, "Random number generators"):
//
//   Rng_Next (0x009B7610): MT19937 step on the state whose address is in EAX (index at state+0x9C0).
//   World_ctor seeds world+0xB4 from the clock; the save key that could reseed it (token 0x7A4) is not
//   written, so two machines never share it. Measured 2026-10-13: the `world_rng` index differed on
//   day 2 of a two-instance game.
//
//   Ten sites draw from it with `MOV EAX,[g_world]; ADD EAX,0xB4; CALL Rng_Next`:
//     0x00820D18 FUN_00820760 (AI frame job, host)      0x00421ECF/0x00421F32 FUN_00821DC0
//     0x0083381E FUN_008336F0                            0x00438955 FUN_008386E0
//     0x0043C722 FUN_0083B3D0                            0x0044401D FUN_00843B90 (AI accepts a diplomatic
//     action: reached from Command_CanExecute_A/B/C on every machine, and from the action tooltip
//     locally)                                           0x00446408 FUN_00845AD0
//     0x0045CFBF AI_DailyRoll (every machine, every day) 0x004687EA
//
// Rule: while the session manager executes a command (its +0x234 flag, set around every command
// including the hourly tick that runs DayTick), every machine runs the same code in the same order,
// so the draw comes from the global generator and is counted like the game's own draws (so the daily
// check covers it). Outside that (AI frame jobs, UI) the draw comes from a private MT19937.

namespace cp::features {

namespace {

constexpr uint32_t kRngNextRva = 0x5B7610;
constexpr uint32_t kMtStateRva = 0xB0ECF0;   // g_mtState
constexpr uint32_t kRngCallsRva = 0xB20C14;  // g_rngCalls
constexpr uint32_t kInGameIdlerVtableRva = 0xA0A34C;  // CInGameIdler::vftable
constexpr uint32_t kIdlerSessionManagerSlot = 0x44;    // vt[17]() -> session manager
constexpr uint32_t kSessionManagerExecuting = 0x234;
constexpr uint32_t kSiteRvas[] = {0x420D18, 0x421ECF, 0x421F32, 0x43381E, 0x438955,
                                  0x43C722, 0x44401D, 0x446408, 0x45CFBF, 0x4687EA};

uintptr_t g_rng_next = 0;
uintptr_t g_mt_state = 0;
uint32_t* g_rng_calls = nullptr;
int g_sites = 0;
uint32_t g_lockstep_draws = 0, g_private_draws = 0;
bool g_logged = false;

// Private MT19937, seeded from the clock once.
struct Mt {
    uint32_t state[624];
    int index = 625;
    void seed(uint32_t s) {
        state[0] = s;
        for (int i = 1; i < 624; i++) {
            state[i] = 1812433253u * (state[i - 1] ^ (state[i - 1] >> 30)) + static_cast<uint32_t>(i);
        }
        index = 624;
    }
    uint32_t next() {
        if (index >= 624) {
            if (index == 625) {
                seed(static_cast<uint32_t>(GetTickCount()) ^ static_cast<uint32_t>(GetCurrentProcessId()) * 2654435761u);
            }
            for (int i = 0; i < 624; i++) {
                uint32_t y = (state[i] & 0x80000000u) | (state[(i + 1) % 624] & 0x7fffffffu);
                state[i] = state[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
            }
            index = 0;
        }
        uint32_t y = state[index++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        return y ^ (y >> 18);
    }
};
Mt g_private;

bool in_command_execution() {
    uintptr_t world = game::world();
    if (!world) {
        return false;
    }
    uintptr_t idler = game::read32(world + 0xB24);
    if (!idler || game::read32(idler) != hooks::exe_addr(kInGameIdlerVtableRva)) {
        return false;  // not the in-game idler: no session manager accessor
    }
    using GetFn = uintptr_t(__thiscall*)(uintptr_t);
    uintptr_t mgr = reinterpret_cast<GetFn>(game::read32(game::read32(idler) + kIdlerSessionManagerSlot))(idler);
    return mgr && *reinterpret_cast<const uint8_t*>(mgr + kSessionManagerExecuting) != 0;
}

uint32_t __cdecl draw() {
    if (in_command_execution()) {
        g_lockstep_draws++;
        (*g_rng_calls)++;
        uint32_t value;
        uintptr_t state = g_mt_state;
        uintptr_t fn = g_rng_next;
        __asm {
            mov eax, state
            call fn
            mov value, eax
        }
        return value;
    }
    g_private_draws++;
    return g_private.next();
}

// Stands in for `CALL Rng_Next` at the ten sites: EAX (the world state) is ignored, the result goes
// back in EAX, ECX/EDX are preserved in case the caller keeps something there.
__declspec(naked) void draw_thunk() {
    __asm {
        push ecx
        push edx
        call draw
        pop edx
        pop ecx
        ret
    }
}

}  // namespace

void install_world_rng(const Config& config) {
    if (!config.sync_world_rng) {
        logf("sync_world_rng: disabled");
        return;
    }
    g_rng_next = hooks::exe_addr(kRngNextRva);
    g_mt_state = hooks::exe_addr(kMtStateRva);
    g_rng_calls = reinterpret_cast<uint32_t*>(hooks::exe_addr(kRngCallsRva));
    for (uint32_t rva : kSiteRvas) {
        if (hooks::redirect_call(hooks::exe_addr(rva), g_rng_next, reinterpret_cast<uintptr_t>(&draw_thunk))) {
            g_sites++;
        } else {
            logf("sync_world_rng: site %08X does not call Rng_Next as expected, left alone", rva + 0x400000);
        }
    }
    logf("sync_world_rng: active on %d of %d world-RNG draw sites (lockstep draws go to the global generator, counted)",
         g_sites, static_cast<int>(sizeof(kSiteRvas) / sizeof(kSiteRvas[0])));
}

}  // namespace cp::features
