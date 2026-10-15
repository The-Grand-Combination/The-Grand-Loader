#include "features/stability/tech_window_guard.h"

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04. Both sites read invention+0x430 (the status object), then +0x310, then +0x40.
//
// Sort comparator FUN_007A9070 (__fastcall(a, mode, b) -> bool); mode 0 compares that field:
//   0x007A9182  MOV EDX,[ESI+0x430]           EDX = b.status
//   0x007A9188  MOV ECX,[ECX+0x430]           ECX = a.status
//   0x007A918A  MOV EAX,[EDX+0x310]           <- replaced (12 bytes)
//   0x007A9190  MOV EDX,[ECX+0x310]
//   0x007A9196  MOV EAX,[EAX+0x40]; XOR ECX,ECX; CMP EAX,[EDX+0x40]; POP ESI; SETL AL
//   0x007A91A2  MOV ECX,[EBP-0xC] ...         epilogue, AL = result
// With any of the four pointers null the guard returns false (the elements compare equal, which
// keeps the ordering valid).
//
// Folder builder FUN_007ADB70, for invention list[i] (ECX):
//   0x007ADE98  MOV ECX,[ECX+0x430]           <- replaced (15 bytes)
//   0x007ADE9E  MOV ECX,[ECX+0x310]
//   0x007ADEA4  MOV ECX,[ECX+0x40]            the folder name (char*)
//   0x007ADEA7  MOV EDX,[EAX]; PUSH ECX; ...  passed to a string method
// With any pointer null the guard passes an empty name.

namespace cp::features {

namespace {

constexpr uint32_t kSortSiteRva = 0x3A918A;
constexpr uint32_t kSortResumeRva = 0x3A9196;
constexpr uint32_t kSortEpilogueRva = 0x3A91A2;
constexpr unsigned char kSortSiteBytes[] = {0x8B, 0x82, 0x10, 0x03, 0x00, 0x00,   // MOV EAX,[EDX+0x310]
                                            0x8B, 0x91, 0x10, 0x03, 0x00, 0x00};  // MOV EDX,[ECX+0x310]

constexpr uint32_t kFolderSiteRva = 0x3ADE98;
constexpr uint32_t kFolderResumeRva = 0x3ADEA7;
constexpr unsigned char kFolderSiteBytes[] = {0x8B, 0x89, 0x30, 0x04, 0x00, 0x00,  // MOV ECX,[ECX+0x430]
                                              0x8B, 0x89, 0x10, 0x03, 0x00, 0x00,  // MOV ECX,[ECX+0x310]
                                              0x8B, 0x49, 0x40};                   // MOV ECX,[ECX+0x40]

uintptr_t g_sort_resume = 0;
uintptr_t g_sort_epilogue = 0;
uintptr_t g_folder_resume = 0;
const char g_empty_name[1] = "";

__declspec(naked) void sort_guard() {
    __asm {
        test edx, edx
        jz not_less
        test ecx, ecx
        jz not_less
        mov eax, [edx + 0x310]
        test eax, eax
        jz not_less
        mov edx, [ecx + 0x310]
        test edx, edx
        jz not_less
        jmp dword ptr [g_sort_resume]
    not_less:
        pop esi
        xor eax, eax
        jmp dword ptr [g_sort_epilogue]
    }
}

__declspec(naked) void folder_guard() {
    __asm {
        test ecx, ecx
        jz missing
        mov ecx, [ecx + 0x430]
        test ecx, ecx
        jz missing
        mov ecx, [ecx + 0x310]
        test ecx, ecx
        jz missing
        mov ecx, [ecx + 0x40]
        test ecx, ecx
        jz missing
        jmp dword ptr [g_folder_resume]
    missing:
        lea ecx, g_empty_name
        jmp dword ptr [g_folder_resume]
    }
}

}  // namespace

void install_tech_window_guard(const Config& config) {
    if (!config.fix_tech_window_crash) {
        logf("fix_tech_window_crash: disabled");
        return;
    }
    g_sort_resume = hooks::exe_addr(kSortResumeRva);
    g_sort_epilogue = hooks::exe_addr(kSortEpilogueRva);
    g_folder_resume = hooks::exe_addr(kFolderResumeRva);
    int sites = 0;
    if (hooks::jump_hook(hooks::exe_addr(kSortSiteRva), kSortSiteBytes, sizeof(kSortSiteBytes),
                         reinterpret_cast<uintptr_t>(&sort_guard))) {
        sites++;
    } else {
        logf("fix_tech_window_crash: sort site %08X not recognised, left alone", kSortSiteRva + 0x400000);
    }
    if (hooks::jump_hook(hooks::exe_addr(kFolderSiteRva), kFolderSiteBytes, sizeof(kFolderSiteBytes),
                         reinterpret_cast<uintptr_t>(&folder_guard))) {
        sites++;
    } else {
        logf("fix_tech_window_crash: folder site %08X not recognised, left alone", kFolderSiteRva + 0x400000);
    }
    logf("fix_tech_window_crash: active on %d of 2 sites (invention status checked before use)", sites);
}

}  // namespace cp::features
