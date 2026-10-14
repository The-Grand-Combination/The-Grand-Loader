#include "features/profilers/year_timer.h"

#include <windows.h>

#include <cstdint>

#include "hooks/hourly_tick.h"
#include "hooks/memory.h"
#include "log.h"

// The game date is a count of hours at world+0xB0C; days are hours / 24 and years are 365 days with
// no leap years, counted from year -5000 (day 2495140 is in 1836). A year boundary is therefore
// `day / 365` changing. Wall time is sampled on every hourly tick; a gap longer than kPauseGapMs
// between two ticks is treated as a pause (or a menu, or a save) and excluded from the "active" time.

namespace cp::features {

namespace {

constexpr uint32_t kWorldPtrRva = 0xE588E8;  // DAT_012588E8
constexpr uint32_t kWorldDateOffset = 0xB0C;  // hours
constexpr int32_t kDaysPerYear = 365;
constexpr int32_t kYearBase = 5000;
constexpr double kPauseGapMs = 2000.0;

int64_t g_qpc_freq = 0;
int32_t g_year = -1;          // game year being timed
int32_t g_year_start_day = 0;  // first day seen of that year
int64_t g_year_start_qpc = 0;
int64_t g_last_tick_qpc = 0;
double g_paused_ms = 0.0;      // gaps excluded so far in this year
int32_t g_last_day = -1;

inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
inline double ms(int64_t ticks) { return 1000.0 * static_cast<double>(ticks) / static_cast<double>(g_qpc_freq); }

int32_t current_day() {
    uintptr_t world = *reinterpret_cast<const uint32_t*>(hooks::exe_addr(kWorldPtrRva));
    return world ? static_cast<int32_t>(*reinterpret_cast<const uint32_t*>(world + kWorldDateOffset)) / 24 : -1;
}

void on_tick() {
    int32_t day = current_day();
    if (day < 0) {
        return;
    }
    int64_t now = qpc();
    int32_t year = day / kDaysPerYear - kYearBase;

    if (g_year < 0 || day < g_last_day || day > g_last_day + 1) {
        // first tick, or a jump (new game, load): start a fresh measurement
        g_year = year;
        g_year_start_day = day;
        g_year_start_qpc = now;
        g_paused_ms = 0.0;
    } else {
        double gap = ms(now - g_last_tick_qpc);
        if (gap > kPauseGapMs) {
            g_paused_ms += gap;
        }
        if (year != g_year) {
            double total_ms = ms(now - g_year_start_qpc);
            int32_t days = day - g_year_start_day;
            double active_ms = total_ms - g_paused_ms;
            logf("year %d: %d:%02d wall (%d:%02d active, %.0f ms/day) over %d day(s)%s", g_year,
                 static_cast<int>(total_ms / 60000.0), static_cast<int>(total_ms / 1000.0) % 60,
                 static_cast<int>(active_ms / 60000.0), static_cast<int>(active_ms / 1000.0) % 60,
                 days > 0 ? active_ms / days : 0.0, days,
                 days < kDaysPerYear ? " (partial year)" : "");
            g_year = year;
            g_year_start_day = day;
            g_year_start_qpc = now;
            g_paused_ms = 0.0;
        }
    }
    g_last_tick_qpc = now;
    g_last_day = day;
}

}  // namespace

void install_year_timer() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;
    if (!hooks::on_hourly_tick(&on_tick)) {
        logf("year_timer: hourly tick hook unavailable, off");
        return;
    }
    logf("year_timer: active, one line per game year");
}

}  // namespace cp::features
