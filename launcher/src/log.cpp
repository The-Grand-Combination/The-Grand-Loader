#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace tgl::log {

namespace {
FILE* g_file = nullptr;
}

void open(std::wstring_view path) {
    if (g_file) {
        return;
    }
    _wfopen_s(&g_file, std::wstring(path).c_str(), L"w");
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
