#include "features/multiplayer/world_rng.h"

#include <windows.h>

#include <cstdint>
#include <iterator>

#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/multiplayer.md, "Random number generators"):
//
//   Rng_Next (0x009B7610): MT19937 step on the state whose address is in EAX (index at state+0x9C0).
//   World_ctor seeds world+0xB4 from the clock; the save key that could reseed it (token 0x7A4) is not
//   written, so two machines never share it (confirmed on a two-instance game: the index differed on
//   day 2, docs/re/log.md 2026-10-13).
//
//   Ten sites draw from it with `MOV EAX,[g_world]; ADD EAX,0xB4; CALL/JMP Rng_Next`:
//     0x00820D18 FUN_00820760 (AI frame job, host)      0x00421ECF/0x00421F32 FUN_00821DC0
//     0x0083381E FUN_008336F0                            0x00438955 FUN_008386E0
//     0x0043C722 FUN_0083B3D0                            0x0044401D FUN_00843B90 (AI accepts a diplomatic
//     action: also reached from local diplomacy dialog updates/tooltips)
//                                                        0x00446408 FUN_00845AD0
//     0x0045CFBF AI_DailyRoll (every machine, every day) 0x004687EA
//
// The executing flag alone is NOT a lockstep boundary: UpdateTimeThrottle pumps local UI inside
// DayTick. Suppress shared draws during that pump, including nested pumps. This covers the inspected
// intra-day path; live multiplayer verification with diplomacy dialogs open is still required.

namespace cp::features {

namespace {

constexpr uint32_t kSiteRvas[] = {0x420D18, 0x421ECF, 0x421F32, 0x43381E, 0x438955,
                                  0x43C722, 0x44401D, 0x446408, 0x45CFBF};
constexpr uint32_t kTailSiteRva = 0x4687EA;  // GetAIRand: JMP, not CALL
constexpr uint32_t kUiPumpCallRva = 0x285727;  // UpdateTimeThrottle -> application pump
constexpr uint32_t kUiPumpRva = 0x5DF2B0;

uintptr_t g_rng_next = 0;
uintptr_t g_mt_state = 0;
uintptr_t g_ui_pump = 0;
// These sites and the private generator run on the game thread, not the trigger workers.
uint32_t g_ui_depth = 0;

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
        // Native Rng_Next's final SAR/XOR clears the sign bit. Callers use signed IDIV and
        // expect nonnegative choices; an unsigned MT output violates that contract.
        return (y ^ (y >> 18)) & 0x7fffffffu;
    }
};
Mt g_private;

bool in_command_execution() {
    uintptr_t mgr = game::session_manager(game::world());
    return mgr && *reinterpret_cast<const uint8_t*>(mgr + game::kSessionManagerExecuting) != 0;
}

uint32_t __cdecl draw() {
    if (g_ui_depth == 0 && in_command_execution()) {
        game::rng_calls()++;
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
    return g_private.next();
}

// The pump takes its application object in ESI and one stack argument (RET 4). Forward an argument
// copy so the original sees its normal stack; keep its return registers, flags and FP state intact.
// A native exception aborts this pump/tick; a retained nonzero depth then fails closed to private RNG.
__declspec(naked) void ui_pump_thunk() {
    __asm {
        pushfd
        inc dword ptr [g_ui_depth]
        popfd
        push dword ptr [esp + 4]
        call dword ptr [g_ui_pump]
        pushfd
        dec dword ptr [g_ui_depth]
        popfd
        ret 4
    }
}

// Stands in for CALL/JMP Rng_Next: EAX (the world state) is ignored, the result goes
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
    g_rng_next = hooks::exe_addr(game::kRngNextRva);
    g_mt_state = hooks::exe_addr(game::kMtStateRva);
    g_ui_pump = hooks::exe_addr(kUiPumpRva);
    const uintptr_t dispatcher = reinterpret_cast<uintptr_t>(&draw_thunk);
    const uintptr_t pump_wrapper = reinterpret_cast<uintptr_t>(&ui_pump_thunk);
    // Establish the UI exclusion before redirecting any draws. Each patch checks its original target;
    // unwind a partial installation rather than mix routed and untouched world draws.
    if (!hooks::redirect_call(hooks::exe_addr(kUiPumpCallRva), g_ui_pump, pump_wrapper)) {
        logf("sync_world_rng: UI pump boundary not recognised, feature off");
        return;
    }
    size_t sites = 0;
    for (; sites < std::size(kSiteRvas); ++sites) {
        if (!hooks::redirect_call(hooks::exe_addr(kSiteRvas[sites]), g_rng_next, dispatcher)) {
            break;
        }
    }
    if (sites != std::size(kSiteRvas) ||
        !hooks::redirect_jump(hooks::exe_addr(kTailSiteRva), g_rng_next, dispatcher)) {
        bool restored = true;
        while (sites > 0) {
            restored = hooks::redirect_call(hooks::exe_addr(kSiteRvas[--sites]), dispatcher, g_rng_next) && restored;
        }
        restored = hooks::redirect_call(hooks::exe_addr(kUiPumpCallRva), pump_wrapper, g_ui_pump) && restored;
        logf("sync_world_rng: unexpected/unwritable draw site, %s",
             restored ? "original routes restored; feature off" : "ROLLBACK FAILED; restart the game");
        return;
    }
    logf("sync_world_rng: active on all 10 draw sites; intra-day UI draws stay private (live MP verification required)");
}

}  // namespace cp::features
