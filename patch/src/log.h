#pragma once

#include <cstdarg>
#include <cstdio>

#include "v2cp/plugin_api.h"

namespace cp {

const V2CP_HostApi& host();

inline void logf(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    host().log(&host(), buf);
}

}  // namespace cp
