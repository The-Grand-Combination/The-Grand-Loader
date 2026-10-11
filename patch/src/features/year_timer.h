#pragma once

namespace cp::features {

// Logs how long each game year took in wall-clock time (total, and with pauses longer than two seconds
// left out), so a run can be timed from v2cp.log instead of a stopwatch. Always on; one line per year.
void install_year_timer();

}  // namespace cp::features
