#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <string>

namespace tgl::log {

namespace {
FILE* g_file = nullptr;
}

void open(std::wstring_view path) {
    if (g_file) {
        return;
    }
    // Deny other writers while this instance runs, so a second instance started from the same folder
    // (a local multiplayer test) cannot truncate this file; it gets its own, named by process id.
    std::wstring primary(path);
    g_file = _wfsopen(primary.c_str(), L"w", _SH_DENYWR);
    if (g_file) {
        return;
    }
    size_t dot = primary.rfind(L'.');
    std::wstring stem = dot == std::wstring::npos ? primary : primary.substr(0, dot);
    std::wstring ext = dot == std::wstring::npos ? L"" : primary.substr(dot);
    std::wstring secondary = stem + L"." + std::to_wstring(GetCurrentProcessId()) + ext;
    g_file = _wfsopen(secondary.c_str(), L"w", _SH_DENYWR);
    if (g_file) {
        line("loader", "the main log is held by another running instance; this instance logs here instead");
    }
}

void line(std::string_view prefix, std::string_view message) {
    if (!g_file) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_file, "[%02u:%02u:%02u.%03u] [%.*s] %.*s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(message.size()),
            message.data());
    fflush(g_file);
}

void linef(std::string_view prefix, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    line(prefix, std::string_view(buf, static_cast<size_t>(n) < sizeof(buf) ? n : sizeof(buf) - 1));
}

}  // namespace tgl::log
