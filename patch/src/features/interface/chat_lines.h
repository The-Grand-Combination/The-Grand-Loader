#pragma once

#include "config.h"

namespace cp::features {

// chat_line_seconds: the in-game chat list (where "X is lagging behind" also appears) drops its oldest
// line once a minute; with this many seconds instead, a line goes away sooner.
void install_chat_lines(const Config& config);

}  // namespace cp::features
