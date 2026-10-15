#include "features/stability/supply_guard.h"

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04, FUN_004D1560 (per-country factory walk, called from FUN_00521FD0), for every
// factory (EDI):
//
//   0x004D15E5  MOV EBX,[EAX+0x12C]        EAX = production type; +0x12C = local-supply source
//   0x004D15EB  MOV EAX,[EBX+0x128]        <- the first read of the source: crashes when it is null
//   ...         three calls on factory+0xE0 with the source as an argument (the callees clean their
//               arguments, so the stack is back at its level of 0x004D15EB afterwards)
//   0x004D1665  MOV EAX,[EDI+0xF4] ...     the employment count, independent of the source
//
// A null source is a production type whose state has no such source. The replaced instruction is
// replayed when EBX is not null; otherwise the walk continues at 0x004D1665. Nothing written in the
// skipped part is read after it (the ECX value stored at [ESP+0x44] is dead).

namespace cp::features {

namespace {

constexpr uint32_t kSiteRva = 0xD15EB;
constexpr uint32_t kResumeRva = 0xD15F1;
constexpr uint32_t kSkipRva = 0xD1665;
constexpr unsigned char kSiteBytes[] = {0x8B, 0x83, 0x28, 0x01, 0x00, 0x00};  // MOV EAX,[EBX+0x128]

uintptr_t g_resume = 0;
uintptr_t g_skip = 0;

__declspec(naked) void guard() {
    __asm {
        test ebx, ebx
        jz skip
        mov eax, [ebx + 0x128]
        jmp dword ptr [g_resume]
    skip:
        jmp dword ptr [g_skip]
    }
}

}  // namespace

void install_supply_guard(const Config& config) {
    if (!config.fix_supply_crash) {
        logf("fix_supply_crash: disabled");
        return;
    }
    g_resume = hooks::exe_addr(kResumeRva);
    g_skip = hooks::exe_addr(kSkipRva);
    if (!hooks::jump_hook(hooks::exe_addr(kSiteRva), kSiteBytes, sizeof(kSiteBytes), reinterpret_cast<uintptr_t>(&guard))) {
        logf("fix_supply_crash: site %08X not recognised, off", kSiteRva + 0x400000);
        return;
    }
    logf("fix_supply_crash: active, a factory whose production type has no local-supply source is skipped");
}

}  // namespace cp::features
