#include "features/profilers/year_timer.h"

#include <cstdint>

#include "game/world.h"
#include "hooks/hourly_tick.h"
#include "log.h"
#include "util/clock.h"

// The game date is a count of hours at world+0xB0C; days are hours / 24 and years are 365 days with
// no leap years, counted from year -5000 (day 2495140 is in 1836). A year boundary is therefore
// `day / 365` changing. Wall time is sampled on every hourly tick; a gap longer than kPauseGapMs
// between two ticks is treated as a pause (or a menu, or a save) and excluded from the "active" time.

namespace cp::features {

namespace {

constexpr int32_t kDaysPerYear = 365;
constexpr int32_t kYearBase = 5000;
constexpr double kPauseGapMs = 2000.0;

int32_t g_year = -1;          // game year being timed
int32_t g_year_start_day = 0;  // first day seen of that year
int64_t g_year_start_qpc = 0;
int64_t g_last_tick_qpc = 0;
double g_paused_ms = 0.0;      // gaps excluded so far in this year
int32_t g_last_day = -1;

void start_year(int32_t year, int32_t day, int64_t now) {
    g_year = year;
    g_year_start_day = day;
    g_year_start_qpc = now;
    g_paused_ms = 0.0;
}

void log_year(int32_t day, int64_t now) {
    double total_ms = util::qpc_ms(now - g_year_start_qpc);
    int32_t days = day - g_year_start_day;
    double active_ms = total_ms - g_paused_ms;
    logf("year %d: %d:%02d wall (%d:%02d active, %.0f ms/day) over %d day(s)%s", g_year,
         static_cast<int>(total_ms / 60000.0), static_cast<int>(total_ms / 1000.0) % 60,
         static_cast<int>(active_ms / 60000.0), static_cast<int>(active_ms / 1000.0) % 60,
         days > 0 ? active_ms / days : 0.0, days, days < kDaysPerYear ? " (partial year)" : "");
}

void on_tick() {
    int32_t day = game::current_day();
    if (day < 0) {
        return;
    }
    int64_t now = util::qpc();
    int32_t year = day / kDaysPerYear - kYearBase;

    if (g_year < 0 || day < g_last_day || day > g_last_day + 1) {
        start_year(year, day, now);  // first tick, or a jump (new game, load): a fresh measurement
    } else {
        double gap = util::qpc_ms(now - g_last_tick_qpc);
        if (gap > kPauseGapMs) {
            g_paused_ms += gap;
        }
        if (year != g_year) {
            log_year(day, now);
            start_year(year, day, now);
        }
    }
    g_last_tick_qpc = now;
    g_last_day = day;
}

}  // namespace

void install_year_timer() {
    if (!hooks::on_hourly_tick(&on_tick)) {
        logf("year_timer: hourly tick hook unavailable, off");
        return;
    }
    logf("year_timer: active, one line per game year");
}

}  // namespace cp::features
