#pragma once

#include "config.h"

namespace se::features {

// income_modifiers: rich_income_modifier, middle_income_modifier and poor_income_modifier scale what the
// pops of that strata earn (wages, profits, artisan sales, pensions, unemployment benefit), counting the
// country's modifiers (technologies, inventions, reforms, national value, country modifiers) and the
// province's. Vanilla applies only the province's, and to an artisan's input budget instead of income.
void install_income_modifiers(const Config& config);

}  // namespace se::features
