#pragma once

#include "config.h"

namespace cp::features {

// Country_PrePass runs for every tag every day. At its start (the call that resets two per-country tables):
//   skip_clean_resets: skip the reset when the tables already hold the reset value everywhere (dead tags);
//   with prefetch_relations: prefetch the relation fields its later loop reads, for tags without provinces;
//   with prefetch_pops: prefetch the tag's pops for the pop walk that follows.
// Same results by construction. See docs/re/systems/performance.md.
void install_country_prepass(const Config& config);

}  // namespace cp::features
