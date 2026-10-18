#include "features/interface/last_ip.h"

#include <cstdint>

#include "game/world.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// v2game.exe 3.04:
//
//   CSettings (singleton DAT_0125B5E0, accessor FUN_00475500): +0x108 lastplayer, +0x124 lasthost,
//   +0x140 serveradress (std::string each; settings.txt tokens 0x27E/0x27F/0x280, parsed by slot 4
//   0x00475750, written by slot 2 0x00475AF0). +0xC0 holds the file name "settings.txt".
//   Settings_Save (0x009D7FA0, settings in ESI): opens that file for writing and calls slot 2. Called by
//   four menus and by the CSettings destructor (0x00475400), which a normal exit does not reach.
//
//   Join IP dialog: FUN_00766B80 (title JOIN_IP_TITLE) sets the "edit" box's text to settings+0x124;
//   its OK handler FUN_00767140 reads the box (vt[0x4C]) and stores it:
//     0x00767322  LEA ECX,[EAX+0x124]
//     0x00767328  CALL std::string::assign (0x00408ED0, thiscall (src, pos, count), RET 0xC)
//
// The CALL goes to a thunk that runs the assignment and then Settings_Save.

namespace cp::features {

namespace {

constexpr uint32_t kAssignCallRva = 0x367328;
constexpr unsigned char kLeaBytes[] = {0x8D, 0x88, 0x24, 0x01, 0x00, 0x00};  // LEA ECX,[EAX+0x124] before it
constexpr uint32_t kSettingsPtrRva = 0xE5B5E0;
constexpr uint32_t kSettingsSaveRva = 0x5D7FA0;

uintptr_t g_assign = 0;
uintptr_t g_save = 0;
bool g_logged = false;

void __cdecl save_settings() {
    uintptr_t settings = game::read32(hooks::exe_addr(kSettingsPtrRva));
    if (!settings) {
        return;
    }
    uintptr_t save = g_save;
    __asm {
        push esi
        mov esi, settings
        call save
        pop esi
    }
    if (!g_logged) {
        g_logged = true;
        logf("save_last_ip: join address stored in settings.txt");
    }
}

// Stands in for the CALL: same three stack arguments and ECX, same RET 0xC and EAX.
__declspec(naked) void assign_thunk() {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        push dword ptr [esp + 12]
        call dword ptr [g_assign]
    }
    CP_SAVE_ALL
    __asm call save_settings
    CP_RESTORE_ALL
    __asm ret 12
}

}  // namespace

void install_last_ip(const Config& config) {
    if (!config.save_last_ip) {
        logf("save_last_ip: disabled");
        return;
    }
    g_assign = hooks::exe_addr(game::kStringAssignRva);
    g_save = hooks::exe_addr(kSettingsSaveRva);
    if (!hooks::expect_bytes(hooks::exe_addr(kAssignCallRva - 6), "\x8D\x88\x24\x01\x00\x00", 6) ||
        !hooks::redirect_call(hooks::exe_addr(kAssignCallRva), g_assign, reinterpret_cast<uintptr_t>(&assign_thunk))) {
        logf("save_last_ip: join dialog site %08X not recognised, off", hooks::listing_va(kAssignCallRva));
        return;
    }
    logf("save_last_ip: active, the join address is written to settings.txt when you join");
}

}  // namespace cp::features
