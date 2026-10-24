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
//
//   Mouse_GetClientPos (0x00A7A970, mouse vt[0x4C] at 0x00E4397C; thiscall, out POINT*, returns it,
//   RET 4): mouse+0xCC HWND, +0xD0 frameless flag. In a framed window it subtracts GetWindowRect plus a
//   hard-coded frame (4 px left, SM_CYDLGFRAME + SM_CYCAPTION top), smaller than the Windows 10/11 frame:
//   the 6 px bands of the top and left edges fall on the title bar and border, outside the client area.

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
constexpr uint32_t kMouseGetPosSlotRva = 0xA4397C;
constexpr uint32_t kMouseGetPosRva = 0x67A970;
constexpr uint32_t kMouseWindow = 0xCC;
constexpr uint32_t kMouseFrameless = 0xD0;
constexpr DWORD kLockStaleMs = 250;  // no camera update for this long: not in the game view, hold released

uintptr_t g_create_window = 0;
uintptr_t g_camera = 0;

// Edge hold (edge_scroll_lock): while the game view is up and focused, the cursor is clipped to the client
// area, so it stops on the edge band. Raw mouse input still reports motion against the clip; pushing
// further out than the tolerance releases the cursor just outside that edge.
int g_lock_pull = 0;
HWND g_window = nullptr;
WNDPROC g_original_proc = nullptr;
bool g_unicode = false;
bool g_clipped = false;
bool g_escaped = false;  // released by a push: not held again until the cursor has been outside
int g_push_x = 0;
int g_push_y = 0;
DWORD g_last_camera = 0;

void release_cursor() {
    if (g_clipped) {
        ClipCursor(nullptr);
        g_clipped = false;
    }
    g_push_x = g_push_y = 0;
}

bool client_screen_rect(HWND window, RECT& out) {
    if (!GetClientRect(window, &out)) {
        return false;
    }
    POINT corners[2] = {{out.left, out.top}, {out.right, out.bottom}};
    if (!ClientToScreen(window, &corners[0]) || !ClientToScreen(window, &corners[1])) {
        return false;
    }
    out = {corners[0].x, corners[0].y, corners[1].x, corners[1].y};
    return out.right > out.left && out.bottom > out.top;
}

// Outward motion against a clipped edge accumulates; motion along or away from it resets.
void on_raw_motion(int dx, int dy) {
    if (!g_clipped) {
        return;
    }
    if (GetTickCount() - g_last_camera > kLockStaleMs) {
        release_cursor();
        return;
    }
    RECT r;
    POINT p;
    if (!client_screen_rect(g_window, r) || !GetCursorPos(&p)) {
        return;
    }
    int out_x = p.x <= r.left ? -dx : p.x >= r.right - 1 ? dx : 0;
    int out_y = p.y <= r.top ? -dy : p.y >= r.bottom - 1 ? dy : 0;
    g_push_x = out_x > 0 ? g_push_x + out_x : (dx != 0 ? 0 : g_push_x);
    g_push_y = out_y > 0 ? g_push_y + out_y : (dy != 0 ? 0 : g_push_y);
    if (g_push_x <= g_lock_pull && g_push_y <= g_lock_pull) {
        return;
    }
    if (g_push_x > g_lock_pull) {
        p.x = dx < 0 ? r.left - 2 : r.right + 1;
    }
    if (g_push_y > g_lock_pull) {
        p.y = dy < 0 ? r.top - 2 : r.bottom + 1;
    }
    release_cursor();
    g_escaped = true;
    SetCursorPos(p.x, p.y);
}

LRESULT CALLBACK lock_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_INPUT: {
        RAWINPUT input;
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
            input.header.dwType == RIM_TYPEMOUSE && !(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            on_raw_motion(input.data.mouse.lLastX, input.data.mouse.lLastY);
        }
        break;
    }
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            release_cursor();
        }
        break;
    case WM_ACTIVATEAPP:
        if (!wparam) {
            release_cursor();
        }
        break;
    case WM_KILLFOCUS:
    case WM_DESTROY:
        release_cursor();
        break;
    }
    return g_unicode ? CallWindowProcW(g_original_proc, window, msg, wparam, lparam)
                     : CallWindowProcA(g_original_proc, window, msg, wparam, lparam);
}

// First camera update: raw mouse input for the game window and a window procedure that reads it.
void start_lock(HWND window) {
    RAWINPUTDEVICE device = {0x01, 0x02, 0, window};  // generic desktop, mouse; legacy messages kept
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        logf("edge_scroll_lock: raw mouse input unavailable (error %lu), off", GetLastError());
        g_lock_pull = 0;
        return;
    }
    g_window = window;
    g_unicode = IsWindowUnicode(window) != 0;  // the A/W pair keeps the window's character set
    LONG_PTR proc = reinterpret_cast<LONG_PTR>(&lock_proc);
    g_original_proc = reinterpret_cast<WNDPROC>(g_unicode ? SetWindowLongPtrW(window, GWLP_WNDPROC, proc)
                                                          : SetWindowLongPtrA(window, GWLP_WNDPROC, proc));
    logf("edge_scroll_lock: active, the cursor stays in the game view until pushed %d mouse counts past an edge",
         g_lock_pull);
}

void update_lock(HWND window, bool inside) {
    if (!g_window) {
        start_lock(window);
        if (!g_lock_pull) {
            return;
        }
    }
    g_last_camera = GetTickCount();
    if (!inside) {
        g_escaped = false;
        release_cursor();
        return;
    }
    RECT r;
    if (!g_escaped && client_screen_rect(window, r)) {
        ClipCursor(&r);  // every frame: the window may have moved
        g_clipped = true;
    }
}

// Mouse vt[0x4C]: the cursor in client coordinates of the game window, whatever its frame.
POINT* __fastcall mouse_client_pos(uintptr_t mouse, void*, POINT* out) {
    HWND window = reinterpret_cast<HWND>(game::read32(mouse + kMouseWindow));
    POINT p = {};
    GetCursorPos(&p);
    if (*reinterpret_cast<const uint8_t*>(mouse + kMouseFrameless) == 0) {
        ScreenToClient(window, &p);
    } else {
        RECT r;
        GetWindowRect(window, &r);
        p.x -= r.left;
        p.y -= r.top;
    }
    *out = p;
    return out;
}

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
        release_cursor();
        return 0;
    }
    POINT cursor;
    RECT client;
    if (!GetCursorPos(&cursor) || !ScreenToClient(window, &cursor) || !GetClientRect(window, &client)) {
        return 1;  // cannot tell: leave the game as it is
    }
    bool inside = PtInRect(&client, cursor) != 0;
    if (g_lock_pull > 0) {
        update_lock(window, inside);
    }
    return inside ? 1 : 0;
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
    if (!hooks::replace_pointer(hooks::exe_addr(kMouseGetPosSlotRva), hooks::exe_addr(kMouseGetPosRva),
                                reinterpret_cast<uintptr_t>(&mouse_client_pos))) {
        logf("fix_edge_scroll: mouse position slot %08X not recognised, frame offset left as is",
             hooks::listing_va(kMouseGetPosSlotRva));
    }
    g_lock_pull = config.edge_scroll_lock > 0 ? config.edge_scroll_lock : 0;
    logf("fix_edge_scroll: active, the map edge-scrolls only with the cursor inside the focused game window");
}

}  // namespace cp::features
