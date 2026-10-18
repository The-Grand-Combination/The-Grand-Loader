#include "features/stability/window_mode.h"

#include <windows.h>

#include <cstdint>

#include "game/world.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/ui-console.md, "Window, focus and mouse"):
//
//   App_CreateWindow (0x009DFA30, __fastcall ECX = app), called once from the app constructor at
//   0x009DEEBF, reads the graphics settings at app+0xEC: +0x40/+0x44 resolution, +0x4C fullScreen,
//   +0x4D borderless. fullScreen: a WS_POPUP window for an exclusive-fullscreen device, which is lost on
//   every alt-tab (Present fails, TestCooperativeLevel, Sleep(1000), Reset of every resource, 10 fps
//   in the background). fullScreen = no, borderless = yes: a WS_POPUP window at (0,0) of the resolution's
//   size with a windowed device (present parameters Windowed = !fullScreen), which alt-tab leaves alone.
//   The device and the mouse read the same settings later, so setting them before the window is created
//   is enough.
//
//   Camera_Update (0x0063F870; ESI = map view, EDI = mouse; stack: input, ?, key list, frame time,
//   edge-scroll flag) scrolls the map when the cursor is within 6 px of the view's edges, measured with
//   the mouse's client position (vt[0x4C]: GetCursorPos - GetWindowRect), which is never checked against
//   the window. CInGameIdler_Update calls it at 0x00656275 with the flag 1 (active window) and at
//   0x00656321 with 0 (inactive). A cursor on another monitor counts as "at the edge".

namespace cp::features {

namespace {

constexpr uint32_t kCreateWindowCallRva = 0x5DEEBF;
constexpr uint32_t kCreateWindowRva = 0x5DFA30;
constexpr uint32_t kAppSettings = 0xEC;
constexpr uint32_t kSettingsWidth = 0x40;
constexpr uint32_t kSettingsHeight = 0x44;
constexpr uint32_t kSettingsFullScreen = 0x4C;
constexpr uint32_t kSettingsBorderless = 0x4D;

constexpr uint32_t kAppPtrRva = 0xB20C38;  // DAT_00F20C38, set by App_CreateWindow
constexpr uint32_t kAppWindow = 0xD8;
constexpr uint32_t kCameraCallRva = 0x256275;  // the active-window call, edge-scroll flag 1
constexpr uint32_t kCameraRva = 0x23F870;
constexpr uint32_t kEdgeFlagArg = 0x14;        // fifth stack argument, after the return address

uintptr_t g_create_window = 0;
uintptr_t g_camera = 0;

void __cdecl force_borderless(uintptr_t app) {
    uintptr_t settings = game::read32(app + kAppSettings);
    if (!settings) {
        return;
    }
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    auto* s = reinterpret_cast<uint8_t*>(settings);
    int old_width = *reinterpret_cast<int*>(s + kSettingsWidth);
    int old_height = *reinterpret_cast<int*>(s + kSettingsHeight);
    bool was_fullscreen = s[kSettingsFullScreen] != 0;
    bool was_borderless = s[kSettingsBorderless] != 0;
    if (width > 0 && height > 0) {
        *reinterpret_cast<int*>(s + kSettingsWidth) = width;
        *reinterpret_cast<int*>(s + kSettingsHeight) = height;
    }
    s[kSettingsFullScreen] = 0;
    s[kSettingsBorderless] = 1;
    logf("borderless_fullscreen: settings %dx%d fullScreen=%d borderless=%d -> %dx%d borderless window", old_width,
         old_height, was_fullscreen ? 1 : 0, was_borderless ? 1 : 0, width, height);
}

// In place of CALL App_CreateWindow: ECX = app.
__declspec(naked) void create_window_thunk() {
    __asm {
        CP_SAVE_ALL
        push ecx
        call force_borderless
        add esp, 4
        CP_RESTORE_ALL
        jmp dword ptr [g_create_window]
    }
}

// True while the game window is in the foreground and the cursor is over its client area.
int __cdecl edge_scroll_allowed() {
    uintptr_t app = game::read32(hooks::exe_addr(kAppPtrRva));
    HWND window = app ? reinterpret_cast<HWND>(game::read32(app + kAppWindow)) : nullptr;
    if (!window || GetForegroundWindow() != window) {
        return 0;
    }
    POINT cursor;
    RECT client;
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor) || !GetClientRect(window, &client)) {
        return 1;  // cannot tell: leave the game as it is
    }
    return PtInRect(&client, cursor) ? 1 : 0;
}

// In place of the active-window CALL Camera_Update: clears the edge-scroll flag when it does not apply.
// ESI and EDI carry arguments, so every register is kept.
__declspec(naked) void camera_thunk() {
    __asm {
        CP_SAVE_ALL
        call edge_scroll_allowed
        test eax, eax
        jnz keep
        mov dword ptr [esp + CP_SAVED_ALL_BYTES + 0x14], 0  ; kEdgeFlagArg
    keep:
        CP_RESTORE_ALL
        jmp dword ptr [g_camera]
    }
}

}  // namespace

void install_borderless_fullscreen(const Config& config) {
    if (!config.borderless_fullscreen) {
        logf("borderless_fullscreen: disabled");
        return;
    }
    g_create_window = hooks::exe_addr(kCreateWindowRva);
    if (!hooks::redirect_call(hooks::exe_addr(kCreateWindowCallRva), g_create_window,
                              reinterpret_cast<uintptr_t>(&create_window_thunk))) {
        logf("borderless_fullscreen: call site %08X not recognised, off", hooks::listing_va(kCreateWindowCallRva));
        return;
    }
    logf("borderless_fullscreen: active, the game window is a borderless window at the monitor's size");
}

void install_edge_scroll_fix(const Config& config) {
    if (!config.fix_edge_scroll) {
        logf("fix_edge_scroll: disabled");
        return;
    }
    g_camera = hooks::exe_addr(kCameraRva);
    if (!hooks::redirect_call(hooks::exe_addr(kCameraCallRva), g_camera, reinterpret_cast<uintptr_t>(&camera_thunk))) {
        logf("fix_edge_scroll: call site %08X not recognised, off", hooks::listing_va(kCameraCallRva));
        return;
    }
    logf("fix_edge_scroll: active, the map edge-scrolls only with the cursor inside the focused game window");
}

}  // namespace cp::features
