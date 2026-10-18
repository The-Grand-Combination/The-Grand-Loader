#include "features/interface/clipboard_paste.h"

#include <windows.h>

#include "hooks/iat.h"
#include "log.h"

// v2game.exe 3.04: the frame loops pump messages with PeekMessageA / TranslateMessage /
// DispatchMessageA (calls through the import slot 0x00C8A310 at 0x009DF308 and 0x009DF6A9). The window
// procedure (0x009DFC70, registered by FUN_009DFA30 through the stub 0x009E07F0) handles WM_CHAR: unless
// the input manager (DAT_00F20C38+0xE8, vt[0x30]) refuses it, a character above 13 goes to the focused
// text field (focus = window+0x80, vt[0x34] "takes text", vt[0x28] insert). TranslateMessage turns
// Ctrl+V into WM_CHAR 0x16, which the field drops. Characters dispatched while Ctrl is still held do
// not reach the field; the input manager's check most likely refuses characters while Ctrl is down
// (not confirmed in the code).
//
// The exe's import of DispatchMessageA goes to a wrapper. A WM_CHAR 0x16 is swallowed and the clipboard
// text kept; once Ctrl is up, it is posted to the same window as WM_CHAR messages, which reach the field
// as if typed.

namespace cp::features {

namespace {

constexpr WPARAM kCtrlV = 0x16;
constexpr int kMaxPasted = 256;

using DispatchFn = LRESULT(WINAPI*)(const MSG*);
DispatchFn g_dispatch = nullptr;
unsigned char g_pending[kMaxPasted];
int g_pending_count = 0;
HWND g_pending_window = nullptr;
int g_pastes_logged = 0;

// Printable characters of the clipboard's ANSI text, line breaks and tabs dropped, leading and
// trailing spaces trimmed (a copied address often carries them), at most kMaxPasted.
int read_clipboard(HWND owner, unsigned char* out) {
    if (!IsClipboardFormatAvailable(CF_TEXT) || !OpenClipboard(owner)) {
        return 0;
    }
    int count = 0;
    if (HANDLE data = GetClipboardData(CF_TEXT)) {
        if (const auto* text = static_cast<const unsigned char*>(GlobalLock(data))) {
            for (const unsigned char* c = text; *c && count < kMaxPasted; c++) {
                if ((*c >= 0x20 && *c < 0x7F) || *c >= 0xA0) {
                    out[count++] = *c;
                }
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    int begin = 0;
    while (begin < count && out[begin] == ' ') {
        begin++;
    }
    while (count > begin && out[count - 1] == ' ') {
        count--;
    }
    for (int i = begin; i < count; i++) {
        out[i - begin] = out[i];
    }
    return count - begin;
}

LRESULT WINAPI dispatch_hook(const MSG* msg) {
    if (msg->message == WM_CHAR && msg->wParam == kCtrlV) {
        g_pending_count = read_clipboard(msg->hwnd, g_pending);
        g_pending_window = msg->hwnd;
        if (g_pastes_logged++ < 5) {
            logf("clipboard_paste: Ctrl+V, %d character(s) on the clipboard", g_pending_count);
        }
        return 0;
    }
    LRESULT result = g_dispatch(msg);
    if (g_pending_count > 0 && (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0) {
        for (int i = 0; i < g_pending_count; i++) {
            PostMessageA(g_pending_window, WM_CHAR, g_pending[i], 1);
        }
        g_pending_count = 0;
    }
    return result;
}

}  // namespace

void install_clipboard_paste(const Config& config) {
    if (!config.clipboard_paste) {
        logf("clipboard_paste: disabled");
        return;
    }
    void* original = hooks::hook_iat(GetModuleHandleW(nullptr), "user32.dll", "DispatchMessageA",
                                     reinterpret_cast<void*>(&dispatch_hook));
    if (!original) {
        logf("clipboard_paste: DispatchMessageA import not found, off");
        return;
    }
    g_dispatch = reinterpret_cast<DispatchFn>(original);
    logf("clipboard_paste: active, Ctrl+V types the clipboard text into text fields");
}

}  // namespace cp::features
