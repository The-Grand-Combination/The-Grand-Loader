#include "features/multiplayer/oos_notice.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "features/multiplayer/state_checksum.h"
#include "game/window.h"
#include "game/world.h"
#include "log.h"

// The daily sync check compares the vector of this machine with the host's (docs/re/systems/
// multiplayer.md). Its first two elements are the game's own (draw count, seed); a difference makes the
// game realign the generator (fix_rng_resync: to the host's position), show the OOS dialog once and send
// CPauseGame, both behind the flag world+0xBA8. The elements after them, from state_checksum, hash the
// provinces, countries, pops and the date as of the end of the host's previous day.
//
// - draws differ, state hashes equal: the generator is realigned and the state matched, so the game
//   continues as one. world+0xBA8 is set before the comparison (no dialog, no pause) and a notice says so.
// - draws differ, state differs (or no hashes to tell): the game reacts as usual; the notice says what
//   differs.
// - draws equal, state differs: nothing in the game reacts (a silent divergence); the notice says so once
//   per episode.
//
// The notice is a small topmost window over the top centre of the game window, hidden after
// kShowMs. Exclusive fullscreen hides it, like any other window.

namespace cp::features {

namespace {

constexpr DWORD kShowMs = 15000;
constexpr int kWidth = 720;
constexpr int kHeight = 30;
constexpr int kTop = 48;
constexpr UINT kTimerId = 1;

bool g_enabled = false;
CRITICAL_SECTION g_lock;
char g_text[256];
DWORD g_hide_at = 0;
volatile LONG g_changed = 0;
HWND g_window = nullptr;
HFONT g_font = nullptr;
bool g_thread_started = false;
bool g_diverged = false;  // the last comparison found the state different

void format_date(int32_t day, char* out, size_t size) {
    static const int kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int year = day / 365 - 5000;
    int rest = day % 365;
    int month = 0;
    while (month < 11 && rest >= kMonthDays[month]) {
        rest -= kMonthDays[month++];
    }
    std::snprintf(out, size, "%d.%d.%d", year, month + 1, rest + 1);
}

void place() {
    HWND game = game::find_main_window();
    bool visible = game && !IsIconic(game) && GetTickCount() < g_hide_at;
    if (!visible) {
        ShowWindow(g_window, SW_HIDE);
        return;
    }
    RECT client;
    GetClientRect(game, &client);
    POINT origin = {0, 0};
    ClientToScreen(game, &origin);
    SetWindowPos(g_window, HWND_TOPMOST, origin.x + (client.right - kWidth) / 2, origin.y + kTop, kWidth, kHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK notice_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_TIMER:
            if (InterlockedExchange(&g_changed, 0)) {
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            place();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH background = CreateSolidBrush(RGB(32, 28, 24));
            FillRect(dc, &r, background);
            DeleteObject(background);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(240, 220, 170));
            HGDIOBJ old = SelectObject(dc, g_font);
            char text[sizeof(g_text)];
            EnterCriticalSection(&g_lock);
            std::memcpy(text, g_text, sizeof(text));
            LeaveCriticalSection(&g_lock);
            DrawTextA(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

DWORD WINAPI notice_thread(void*) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = &notice_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "TGL_OosNotice";
    RegisterClassA(&wc);
    g_font = CreateFontA(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    g_window = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, "oos notice",
                               WS_POPUP | WS_BORDER, 0, 0, kWidth, kHeight, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_window) {
        logf("oos_notice: notice window not created (%lu)", GetLastError());
        return 0;
    }
    SetTimer(g_window, kTimerId, 250, nullptr);
    place();
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        DispatchMessageA(&msg);
    }
    return 0;
}

void show(const char* text) {
    EnterCriticalSection(&g_lock);
    std::snprintf(g_text, sizeof(g_text), "%s", text);
    LeaveCriticalSection(&g_lock);
    g_hide_at = GetTickCount() + kShowMs;
    InterlockedExchange(&g_changed, 1);
    logf("oos_notice: %s", text);
    if (!g_thread_started) {
        g_thread_started = true;
        if (HANDLE h = CreateThread(nullptr, 0, &notice_thread, nullptr, 0, nullptr)) {
            CloseHandle(h);
        }
    }
}

}  // namespace

void install_oos_notice(const Config& config) {
    if (!config.oos_notice) {
        logf("oos_notice: disabled");
        return;
    }
    if (!config.state_checksum) {
        logf("oos_notice: needs state_checksum (the state hashes tell a recovery from a divergence), off");
        return;
    }
    InitializeCriticalSection(&g_lock);
    g_enabled = true;
    logf("oos_notice: active, a recovered out-of-sync shows a %lu-second notice instead of the dialog and pause",
         kShowMs / 1000);
}

bool oos_notice_on_compare(const uint32_t* local, size_t n_local, const uint32_t* host, size_t n_host) {
    if (!g_enabled || n_local < kGameSyncElements || n_host < kGameSyncElements) {
        return false;
    }
    bool draws_differ = false;
    for (size_t i = 0; i < kGameSyncElements; i++) {
        draws_differ |= local[i] != host[i];
    }
    char differing[96] = "";
    size_t n = n_local < n_host ? n_local : n_host;
    size_t state_elements = 0;
    for (size_t i = kGameSyncElements; i < n; i++) {
        const char* name = sync_element_name(i);
        if (std::strcmp(name, "rng_calls_end") == 0) {
            continue;  // a draw count, not state
        }
        state_elements++;
        if (local[i] != host[i]) {
            size_t used = std::strlen(differing);
            std::snprintf(differing + used, sizeof(differing) - used, "%s%s", used ? ", " : "", name);
        }
    }
    bool state_differs = differing[0] != '\0';
    char date[24];
    format_date(game::day(game::world()), date, sizeof(date));
    char text[256];
    bool skip_game_reaction = false;
    if (draws_differ && !state_differs && state_elements > 0) {
        std::snprintf(text, sizeof(text), "Out of sync on %s, recovered: random numbers realigned, game state matches the host.",
                      date);
        show(text);
        skip_game_reaction = true;
    } else if (draws_differ) {
        std::snprintf(text, sizeof(text), "Out of sync on %s: game state differs from the host (%s). Save and reload to resync.",
                      date, state_differs ? differing : "not checked");
        show(text);
    } else if (state_differs && !g_diverged) {
        std::snprintf(text, sizeof(text), "Game state differs from the host on %s (%s). Save and reload to resync.", date,
                      differing);
        show(text);
    }
    g_diverged = state_differs;
    return skip_game_reaction;
}

}  // namespace cp::features
