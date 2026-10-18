#include "features/stability/state_guard.h"

#include <cstdint>
#include <iterator>

#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// v2game.exe 3.04. province+0xE8 is the province's state object, null without an owner. Two
// helpers take a state and read it at once:
//
//   State_HasHostilePresence (0x0048F070, (state, flag) on the stack, RET 8, returns 0/1): starts
//   with State_GetControllerTag(state) and then walks state+0x14 and state+0x04.
//   State_GetControllerTag (0x0048E120, state in ECX, out[2] in EAX): CMP [ECX+0xC],0 first.
//
// Most callers test province+0xE8 before calling; the AI's daily target scan (FUN_00851BD0, from
// AI_TargetScan) does not at 0x00851C6D, for every unit of the country: `province[unit+0xDC]+0xE8`.
// A unit cannot normally stand in a province without an owner, so vanilla never meets the null;
// a mod that uncolonises provinces under armies does (crash: read of 0x0000000C at 0x0048E126).
//
// The guards replace each prologue. A null state makes the first return 0 ("nothing hostile") and
// the second write the no-owner answer its own code uses (tag "---", index 0).
//
// The scan itself, and the unit scoring function FUN_00853B30 (from FUN_00851AD0), also walk the
// unit list of another province's state in place, `MOV EAX,[EAX+0x14]; TEST EAX,EAX; JZ not_found`,
// right after `MOV EAX,[province+0xE8]` with only an equality test before: 0x00851F51 (second crash
// in the user's log, reading 0x14) and 0x00853BEF. Those two get a cave that takes the not-found
// branch for a null state.

namespace cp::features {

namespace {

constexpr uint32_t kPresenceRva = 0x8F070;
constexpr uint32_t kPresenceResumeRva = 0x8F078;
// PUSH EBP; MOV EBP,ESP; SUB ESP,0x24; PUSH EBX; PUSH ESI
constexpr unsigned char kPresenceBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x24, 0x53, 0x56};

constexpr uint32_t kTagRva = 0x8E120;
constexpr uint32_t kTagResumeRva = 0x8E126;
constexpr unsigned char kTagBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08};  // PUSH EBP; MOV EBP,ESP; SUB ESP,8

constexpr uint32_t kLogLimit = 20;

// `MOV EAX,[EAX+0x14]; TEST EAX,EAX` with EAX = a state that may be null; the JZ that follows goes to
// `not_found`.
struct WalkSite {
    uint32_t rva;
    uint32_t resume_rva;     // the JZ after the replaced bytes
    uint32_t not_found_rva;  // its target
};
constexpr unsigned char kWalkBytes[] = {0x8B, 0x40, 0x14, 0x85, 0xC0};
constexpr WalkSite kWalkSites[] = {{0x451F51, 0x451F56, 0x452043}, {0x453BEF, 0x453BF4, 0x453C03}};

uintptr_t g_presence_resume = 0;
uintptr_t g_tag_resume = 0;
uint32_t g_hits = 0;
constexpr int kWalkCount = static_cast<int>(std::size(kWalkSites));
uintptr_t g_walk_resume[kWalkCount];
uintptr_t g_walk_not_found[kWalkCount];

void __cdecl report() {
    if (++g_hits <= kLogLimit) {
        logf("fix_missing_state_crash: a unit stands in a province without a state (no owner); answered 'no state'");
    }
}

__declspec(naked) void presence_guard() {
    __asm {
        cmp dword ptr [esp + 4], 0
        jz none
        push ebp
        mov ebp, esp
        sub esp, 0x24
        push ebx
        push esi
        jmp dword ptr [g_presence_resume]
    }
    __asm none:
    CP_SAVE_ALL
    __asm call report
    CP_RESTORE_ALL
    __asm {
        xor eax, eax
        ret 8
    }
}

__declspec(naked) void tag_guard() {
    __asm {
        test ecx, ecx
        jz none
        push ebp
        mov ebp, esp
        sub esp, 8
        jmp dword ptr [g_tag_resume]
    none:
        mov dword ptr [eax], 0x2D2D2D
        mov dword ptr [eax + 4], 0
        ret
    }
}

__declspec(naked) void walk_guard_scan() {
    __asm {
        test eax, eax
        jz none
        mov eax, [eax + 0x14]
        test eax, eax
        jmp dword ptr [g_walk_resume]
    }
    __asm none:
    CP_SAVE_ALL
    __asm call report
    CP_RESTORE_ALL
    __asm {
        jmp dword ptr [g_walk_not_found]
    }
}

__declspec(naked) void walk_guard_score() {
    __asm {
        test eax, eax
        jz none
        mov eax, [eax + 0x14]
        test eax, eax
        jmp dword ptr [g_walk_resume + 4]
    }
    __asm none:
    CP_SAVE_ALL
    __asm call report
    CP_RESTORE_ALL
    __asm {
        jmp dword ptr [g_walk_not_found + 4]
    }
}

constexpr void (*kWalkGuards[])() = {&walk_guard_scan, &walk_guard_score};

}  // namespace

void install_state_guard(const Config& config) {
    if (!config.fix_missing_state_crash) {
        logf("fix_missing_state_crash: disabled");
        return;
    }
    g_presence_resume = hooks::exe_addr(kPresenceResumeRva);
    g_tag_resume = hooks::exe_addr(kTagResumeRva);
    int sites = 0;
    if (hooks::jump_hook(hooks::exe_addr(kPresenceRva), kPresenceBytes, sizeof(kPresenceBytes), reinterpret_cast<uintptr_t>(&presence_guard))) {
        sites++;
    } else {
        logf("fix_missing_state_crash: site %08X not recognised, left alone", hooks::listing_va(kPresenceRva));
    }
    if (hooks::jump_hook(hooks::exe_addr(kTagRva), kTagBytes, sizeof(kTagBytes), reinterpret_cast<uintptr_t>(&tag_guard))) {
        sites++;
    } else {
        logf("fix_missing_state_crash: site %08X not recognised, left alone", hooks::listing_va(kTagRva));
    }
    for (int i = 0; i < kWalkCount; i++) {
        g_walk_resume[i] = hooks::exe_addr(kWalkSites[i].resume_rva);
        g_walk_not_found[i] = hooks::exe_addr(kWalkSites[i].not_found_rva);
        if (hooks::jump_hook(hooks::exe_addr(kWalkSites[i].rva), kWalkBytes, sizeof(kWalkBytes), reinterpret_cast<uintptr_t>(kWalkGuards[i]))) {
            sites++;
        } else {
            logf("fix_missing_state_crash: site %08X not recognised, left alone", hooks::listing_va(kWalkSites[i].rva));
        }
    }
    logf("fix_missing_state_crash: active on %d of %d sites (a province without a state answers 'none')", sites,
         2 + kWalkCount);
}

}  // namespace cp::features
