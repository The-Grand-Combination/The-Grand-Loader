#pragma once

#include <windows.h>

#include <cstdint>

namespace cp::util {

// QueryPerformanceCounter ticks, and their conversion to milliseconds.
inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline int64_t qpc_frequency() {
    static const int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return frequency;
}

inline double qpc_ms(int64_t ticks) {
    return 1000.0 * static_cast<double>(ticks) / static_cast<double>(qpc_frequency());
}

}  // namespace cp::util
