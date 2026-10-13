#include "features/multiplayer/oos_test.h"

#include <windows.h>

#include <cstdint>

#include "game/world.h"
#include "hooks/hourly_tick.h"
#include "log.h"

// The sync check compares world+0xB74 (written at the start of the previous tick) with the vector the
// host put in the current tick command (docs/re/systems/multiplayer.md). Flipping the first element of
// the local vector right before the comparison makes this machine report "out of sync" through the
// game's own path. On a client only that client goes out of sync; on the host, its own compare still
// passes (the command was built from the same vector) but every client fails on the next tick.
//
// The button lives in a tiny topmost tool window owned by a thread of ours, moved over the game
// window's top-right corner four times a second. Exclusive fullscreen hides it; Ctrl+Shift+O works
// everywhere (polled on the game thread at every tick).

namespace cp::features {

namespace {

constexpr int kWidth = 96;
constexpr int kHeight = 24;
constexpr int kMargin = 6;
constexpr UINT kTimerId = 1;
constexpr int kButtonId = 100;

volatile LONG g_requested = 0;
HWND g_overlay = nullptr;
HWND g_game_window = nullptr;
bool g_thread_started = false;
bool g_hotkey_down = false;

BOOL CALLBACK find_game_window(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }
    if (hwnd == g_overlay) {
        return TRUE;
    }
    RECT r;
    GetClientRect(hwnd, &r);
    if (r.right < 400 || r.bottom < 300) {
        return TRUE;  // not the main window
    }
    g_game_window = hwnd;
    return FALSE;
}

void reposition() {
    if (!g_game_window || !IsWindow(g_game_window)) {
        g_game_window = nullptr;
        EnumWindows(&find_game_window, 0);
        if (!g_game_window) {
            ShowWindow(g_overlay, SW_HIDE);
            return;
        }
    }
    RECT client;
    GetClientRect(g_game_window, &client);
    POINT origin = {0, 0};
    ClientToScreen(g_game_window, &origin);
    bool visible = !IsIconic(g_game_window) && IsWindowVisible(g_game_window);
    SetWindowPos(g_overlay, HWND_TOPMOST, origin.x + client.right - kWidth - kMargin, origin.y + kMargin, kWidth, kHeight,
                 SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

LRESULT CALLBACK overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wparam) == kButtonId) {
                InterlockedExchange(&g_requested, 1);
            }
            return 0;
        case WM_TIMER:
            reposition();
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

DWORD WINAPI overlay_thread(void*) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = &overlay_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "TGL_OosTestOverlay";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    g_overlay = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, "oos", WS_POPUP, 0, 0,
                                kWidth, kHeight, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_overlay) {
        logf("oos_test_button: overlay window not created (%lu)", GetLastError());
        return 0;
    }
    HWND button = CreateWindowExA(0, "BUTTON", "Force OOS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, kWidth, kHeight,
                                  g_overlay, reinterpret_cast<HMENU>(static_cast<uintptr_t>(kButtonId)), wc.hInstance, nullptr);
    SendMessageA(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetTimer(g_overlay, kTimerId, 250, nullptr);
    reposition();
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

void on_tick() {
    if (!g_thread_started) {
        g_thread_started = true;  // threads cannot be created from TGL_PluginInit (loader lock)
        HANDLE h = CreateThread(nullptr, 0, &overlay_thread, nullptr, 0, nullptr);
        if (h) {
            CloseHandle(h);
        }
    }
    bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                (GetAsyncKeyState('O') & 0x8000);
    if (down && !g_hotkey_down) {
        InterlockedExchange(&g_requested, 1);
    }
    g_hotkey_down = down;

    if (InterlockedExchange(&g_requested, 0) == 0) {
        return;
    }
    uintptr_t world = game::world();
    if (!world) {
        return;
    }
    uint32_t* begin = reinterpret_cast<uint32_t*>(game::read32(world + game::kWorldSyncVector));
    uint32_t* end = reinterpret_cast<uint32_t*>(game::read32(world + game::kWorldSyncVector + 4));
    if (!begin || end == begin) {
        logf("oos_test_button: sync vector empty, nothing to corrupt (not in a game yet?)");
        return;
    }
    begin[0] ^= 1;
    logf("oos_test_button: forced: local sync element 0 flipped to %08X on day %d; the next comparison fails here (and on every client, when this is the host)", begin[0],
         static_cast<int>(game::read32(world + game::kWorldDate)) / 24);
}

}  // namespace

void install_oos_test(const Config& config) {
    if (!config.oos_test_button) {
        logf("oos_test_button: disabled");
        return;
    }
    if (!hooks::on_hourly_tick(&on_tick)) {
        logf("oos_test_button: hourly tick hook unavailable, off");
        return;
    }
    logf("oos_test_button: active (button over the game window's top-right corner, or Ctrl+Shift+O)");
}

}  // namespace cp::features
