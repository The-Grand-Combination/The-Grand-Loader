#pragma once

#include <cstdarg>
#include <cstdio>

#include "tgl/plugin_api.h"

namespace cp {

const TGL_HostApi& host();

inline void logf(_Printf_format_string_ const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    host().log(&host(), buf);
}

}  // namespace cp
