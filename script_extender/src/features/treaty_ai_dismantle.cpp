#include "features/treaty_ai_dismantle.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "features/treaty.h"
#include "features/treaty_ui.h"
#include "game/map.h"
#include "game/objects.h"
#include "hooks/memory.h"
#include "log.h"

// The AI dismantler: docs/re/systems/treaties.md ("AI dismantler"), colonisation.md (colonial range).
//
// Every term the forced treaty's editor would offer is a Candidate, valued once by its war score and the
// CB's weights (dismantle_candidates). The treaty is then built one term per round: each round the best
// value per point as the treaty stands (Picker::worth: the land rules, which depend on what the treaty
// already moves) among the terms not yet rejected (Picker::rejected: the budget, the loser's limits, the
// loser's contiguity, BADBOY_LIMIT), then the bigger term. A term worth 0 now (an exclave, a partition
// with no freed land next to it) waits for a later round; a rejected one is dropped.
//
// The land rules, in the order worth() applies them to a term that moves land:
//   1. a single province (po_demand_province) only next to the gainer's land or land it gets here;
//   2. the loser's homeland off the gainer's own continent only next to land the gainer owned before,
//      at treaty_dismantle_ai_abroad%;
//   3. land that borders the gainer, or land it gets here: treaty_dismantle_ai_border%;
//   4. a colony only within the gainer's colonial range (the game's: Country_HasColonialRangeTo);
//   5. the first state of a country yet to exist: full value, _adjacent% next to other freed land;
//   6. a colony, or land of interest, may be taken across the sea; on a treaty_dismantle_ai_coastal_only
//      continent only on the coast; a colony is x _home_continent% on the gainer's own continent and
//      x _colony_repeat% per colony already taken on a near or coastal-only continent;
//   7. anything else is an exclave: treaty_dismantle_ai_exclave% (0: never).
// Land of interest (common/ai_interests.txt) is exempt from 1, 2 and 4. Every land value is also x the
// distance factor of the near continents (treaty_dismantle_ai_near / _far / _near_range) where applied.
// Reads only: the same result on every machine.

namespace se::features::treaty_ai {

namespace {

using game::GameString;
using game::read;

constexpr uint32_t kCbName = 0x14;  // std::string

// Per continent of map/continent.txt: named in the two ini lists.
struct ContinentLists {
    std::vector<bool> coastal_only;
    std::vector<bool> near;
};

std::vector<std::string> name_list(std::string text) {
    std::vector<std::string> out;
    for (char& ch : text) {
        ch = ch == ',' || ch == ';' || ch == '\t' ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    for (size_t at = 0; at < text.size();) {
        size_t space = text.find(' ', at);
        space = space == std::string::npos ? text.size() : space;
        if (space > at) {
            out.push_back(text.substr(at, space - at));
        }
        at = space + 1;
    }
    return out;
}

ContinentLists continent_lists(const Config& k) {
    const game::Continents& c = game::continents();
    std::vector<std::string> coastal_only = name_list(k.treaty_dismantle_ai_coastal_only);
    std::vector<std::string> near = name_list(k.treaty_dismantle_ai_near_continents);
    ContinentLists lists;
    for (const std::string& name : c.names) {
        lists.coastal_only.push_back(std::find(coastal_only.begin(), coastal_only.end(), name) != coastal_only.end());
        lists.near.push_back(std::find(near.begin(), near.end(), name) != near.end());
    }
    return lists;
}

// One of the states has a province on a continent flagged in `flags`.
bool states_on(const std::vector<uintptr_t>& states, const std::vector<bool>& flags) {
    for (uintptr_t state : states) {
        for (uint32_t id : game::state_provinces(state)) {
            int index = game::continent_of(id);
            if (index >= 0 && flags[static_cast<size_t>(index)]) {
                return true;
            }
        }
    }
    return false;
}

bool states_on_continent(const std::vector<uintptr_t>& states, int continent) {
    if (continent < 0) {
        return false;
    }
    for (uintptr_t state : states) {
        for (uint32_t id : game::state_provinces(state)) {
            if (game::continent_of(id) == continent) {
                return true;
            }
        }
    }
    return false;
}

struct Candidate {
    treaty::Term term;
    int cost;       // points of the budget it spends
    int64_t value;  // x100: its war score, x core_gain for the winner's cores, x the CB's weight, x ally, x interest, x infamy
    std::vector<uintptr_t> states;  // the loser's states it moves (a single province: its state); empty: no land
    int gainer;                     // who gets the land: the third country when there is one, else the winner
    bool one_province;              // po_demand_province: the province, not the state
    bool colonial;                  // every state colonial
    bool coastal;                   // a state has a coastal province
    bool interest;                  // land of the gainer's interest (common/ai_interests.txt)
    int infamy;                     // x1000: what it costs the dismantler (dismantle_infamy CBs), else 0
    int near;                       // percent by the distance to the gainer's land on a near continent, else 100
    bool home_continent;            // a state on the continent of the gainer's capital
    bool coastal_only_continent;    // a state on a treaty_dismantle_ai_coastal_only continent
    bool near_continent;            // a state on a treaty_dismantle_ai_near_continents continent
    bool in_colonial_range;         // a coastal province within the gainer's colonial range (colonies, existing gainer)
};

// Land the treaty already moves: the loser's state, or one province of it, and who gets it.
struct Moved {
    uintptr_t state;
    uint32_t province;  // non-zero: that province only (a po_demand_province term)
    int gainer;
    bool listed_colony;  // colonial, on a near or coastal-only continent: each one taken discounts the next
};

bool moves(const Moved& m, uint32_t province, uintptr_t state) {
    return m.province ? m.province == province : m.state == state;
}

// Percent of a land term's value by its distance to the gainer's provinces on a near continent:
// 100 + treaty_dismantle_ai_near at distance 0, falling to treaty_dismantle_ai_far at near_range map units.
int near_percent(const Config& k, const std::vector<uintptr_t>& states, int gainer, bool near_continent) {
    if (k.treaty_dismantle_ai_near <= 0 || states.empty() || !near_continent || !game::country_exists(gainer)) {
        return 100;
    }
    float distance = game::distance_to_country(states, gainer);
    float range = static_cast<float>(k.treaty_dismantle_ai_near_range);
    float far = static_cast<float>(k.treaty_dismantle_ai_far);
    if (distance < 0) {
        return 100;
    }
    if (distance >= range) {
        return k.treaty_dismantle_ai_far;
    }
    float near = static_cast<float>(100 + k.treaty_dismantle_ai_near);
    return static_cast<int>(far + (near - far) * (range - distance) / range);
}

// Calls visit(province, state) for every province outside `states` that borders one of them, until it
// returns true.
template <class Visit>
bool borders_where(const std::vector<uintptr_t>& states, Visit visit) {
    for (uintptr_t state : states) {
        for (uint32_t id : game::state_provinces(state)) {
            for (uint32_t other : game::neighbours(id)) {
                uintptr_t other_state = read<uintptr_t>(game::province(other) + game::kProvinceState);
                if (std::find(states.begin(), states.end(), other_state) == states.end() && visit(other, other_state)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// One of `states` borders a province `gainer` owns, or (with_moved) land the treaty gives it.
bool borders_gainer(const std::vector<uintptr_t>& states, int gainer, const std::vector<Moved>& moved, bool with_moved) {
    bool owns = game::country_exists(gainer);
    return borders_where(states, [&](uint32_t province, uintptr_t state) {
        if (owns && read<int>(game::province(province) + game::kProvinceOwner) == gainer) {
            return true;
        }
        return with_moved && std::any_of(moved.begin(), moved.end(),
                                         [&](const Moved& m) { return m.gainer == gainer && moves(m, province, state); });
    });
}

// One of `states` borders land the treaty gives to someone other than `gainer`.
bool borders_others(const std::vector<uintptr_t>& states, int gainer, const std::vector<Moved>& moved) {
    return borders_where(states, [&](uint32_t province, uintptr_t state) {
        return std::any_of(moved.begin(), moved.end(),
                           [&](const Moved& m) { return m.gainer != gainer && moves(m, province, state); });
    });
}

// The province itself borders a province `gainer` owns or land the treaty gives it.
bool province_borders(uint32_t province, int gainer, const std::vector<Moved>& moved) {
    bool owns = game::country_exists(gainer);
    for (uint32_t other : game::neighbours(province)) {
        uintptr_t other_state = read<uintptr_t>(game::province(other) + game::kProvinceState);
        if (owns && read<int>(game::province(other) + game::kProvinceOwner) == gainer) {
            return true;
        }
        if (std::any_of(moved.begin(), moved.end(), [&](const Moved& m) { return m.gainer == gainer && moves(m, other, other_state); })) {
            return true;
        }
    }
    return false;
}

// The loser's states the treaty leaves it, without `taken`.
std::vector<uintptr_t> states_left_to(int loser, const std::vector<Moved>& moved, const std::vector<uintptr_t>& taken) {
    std::vector<uintptr_t> out;
    for (uintptr_t node = read<uintptr_t>(game::country(loser) + game::kCountryStates); node; node = read<uintptr_t>(node + 8)) {
        uintptr_t state = read<uintptr_t>(node);
        bool gone = std::find(taken.begin(), taken.end(), state) != taken.end() ||
                    std::any_of(moved.begin(), moved.end(), [&](const Moved& m) { return !m.province && m.state == state; });
        if (state && !gone) {
            out.push_back(state);
        }
    }
    return out;
}

// Taking `states` would split what the loser keeps into more pieces: an enclave or a detached part.
bool splits_loser(int loser, const std::vector<Moved>& moved, const std::vector<uintptr_t>& states) {
    std::vector<uintptr_t> after = states_left_to(loser, moved, states);
    return !after.empty() && game::components(after) > game::components(states_left_to(loser, moved, {}));
}

// Every term the winner could impose on the loser now (as the editor offers them), with its value and
// the facts the land rules read.
std::vector<Candidate> candidates(const Config& k, uintptr_t action, int winner, int loser, const ContinentLists& lists) {
    std::vector<Candidate> out;
    int count = read<int>(read<uintptr_t>(hooks::exe_va(game::kCountryManager)));
    for (uintptr_t cb : treaty::treaty_cbs()) {
        // free terms are in already; no CB weighed 0 (land that costs infamy: per term, below)
        if (!treaty::is_dismantling(cb) || treaty::is_free(cb) || treaty::ai_weight(cb) <= 0 ||
            treaty::cb_unsupported(cb) || !treaty::cb_usable(cb, winner, loser)) {
            continue;
        }
        std::vector<int> thirds{0};
        if (treaty::cb_needs_country(cb)) {
            thirds.clear();
            for (int c = 1; c < count; ++c) {
                if (c != winner && c != loser && treaty::country_allowed(cb, winner, loser, c)) {
                    thirds.push_back(c);
                }
            }
        }
        bool one_province = treaty_ui::takes_one_province(cb);
        std::vector<uint32_t> places{0};
        if (treaty::cb_needs_province(cb)) {
            places.clear();
            std::vector<uintptr_t> listed;  // one place per state, unless the term takes one province
            for (uint32_t id : game::owned_provinces(loser)) {
                uintptr_t state = read<uintptr_t>(game::province(id) + game::kProvinceState);
                if (!one_province && std::find(listed.begin(), listed.end(), state) != listed.end()) {
                    continue;
                }
                listed.push_back(state);
                places.push_back(id);
            }
        }
        for (int third : thirds) {
            for (uint32_t province : places) {
                treaty::Term term{cb, winner, loser, province, third};
                if (!treaty::term_allowed(action, term) || treaty::term_cost(term) <= 0) {
                    continue;  // not now, or nothing to weigh it by
                }
                Candidate c{};
                c.term = term;
                c.cost = treaty::forced_cost(term);
                c.gainer = third ? third : winner;
                c.one_province = one_province;
                c.states = treaty::states_moved(term);
                c.colonial = !c.states.empty() && std::all_of(c.states.begin(), c.states.end(), game::is_colonial_state);
                c.coastal = std::any_of(c.states.begin(), c.states.end(), game::is_coastal_state);
                c.interest = one_province ? treaty::province_of_interest(province, c.gainer) : treaty::of_interest(c.states, c.gainer);
                c.home_continent = states_on_continent(c.states, game::capital_continent(c.gainer));
                c.coastal_only_continent = states_on(c.states, lists.coastal_only);
                c.near_continent = states_on(c.states, lists.near);
                c.near = near_percent(k, c.states, c.gainer, c.near_continent);
                c.in_colonial_range = false;
                if (c.colonial && game::country_exists(c.gainer)) {
                    for (uintptr_t state : c.states) {
                        for (uint32_t id : game::state_provinces(state)) {
                            if (game::is_coastal(id) && game::in_colonial_range(id, c.gainer)) {
                                c.in_colonial_range = true;
                            }
                        }
                    }
                }
                if (third && treaty::goes_to_third(cb) && c.colonial && !treaty::is_great_power(third) &&
                    !treaty::is_secondary_power(third)) {
                    continue;  // no colonies for a minor ally
                }
                int64_t value = static_cast<int64_t>(treaty::term_cost(term)) * 100;
                if (!third && treaty::is_core_of(province, winner)) {
                    value = value * k.treaty_ai_core_gain / 100;
                }
                value = value * treaty::ai_weight(cb) / 100;
                if (third && treaty::allied(winner, third)) {
                    value = value * k.treaty_dismantle_ai_ally / 100;  // an ally gets it (cores, colonies)
                }
                c.infamy = 0;
                if (treaty::costs_infamy_when_forced(cb)) {
                    if (k.treaty_dismantle_ai_infamy <= 0 && !c.interest) {
                        continue;  // land without a claim only with treaty_dismantle_ai_infamy, or of interest
                    }
                    c.infamy = treaty::term_infamy(term);
                    if (!c.interest) {
                        value = value * k.treaty_dismantle_ai_infamy / 100;  // less likely
                    }
                }
                if (c.interest) {
                    value = value * k.treaty_dismantle_ai_interest / 100;  // after cores, before colonies
                }
                c.value = value;
                out.push_back(std::move(c));
            }
        }
    }
    return out;
}

// The treaty being built and the limits it keeps.
class Picker {
public:
    Picker(const Config& k, uintptr_t action, int winner, int loser, int budget)
        : k_(k), action_(action), winner_(winner), loser_(loser), budget_(budget) {
        limit_ = budget + std::max(k.treaty_dismantle_ai_overspend, 0);
        spent_ = treaty::points(action);
        states_left_ = read<int>(game::country(loser) + game::kCountryStateCount) - 1;
        homeland_left_ = game::homeland_states(loser) * std::max(k.treaty_dismantle_ai_max_states, 0) / 100;
    }

    // The term's value as the treaty stands, 0 when it cannot be taken now, and the land rule that decided.
    int64_t worth(const Candidate& c, const char** why) const {
        auto say = [&](const char* reason) { *why = reason; };
        if (c.states.empty()) {
            say("no land");
            return c.value;
        }
        if (treaty::is_partition(c.term.cb) && !borders_others(c.states, winner_, moved_)) {
            say("partition not next to freed land");  // partitions extend freed land, they do not start it
            return 0;
        }
        if (c.one_province && !c.interest && !province_borders(c.term.province, c.gainer, moved_)) {
            say("province not adjacent");
            return 0;
        }
        int64_t near = c.near;
        bool gainer_exists = game::country_exists(c.gainer);
        if (!c.colonial && !c.interest && gainer_exists && !c.home_continent) {
            // a foothold the peace gave it, say: only next to land it owned before, never chained
            if (k_.treaty_dismantle_ai_abroad <= 0 || !borders_gainer(c.states, c.gainer, moved_, false)) {
                say("homeland off the gainer's continent");
                return 0;
            }
            say("abroad, borders land the gainer already owned");
            return c.value * k_.treaty_dismantle_ai_border / 100 * near / 100 * k_.treaty_dismantle_ai_abroad / 100;
        }
        if (borders_gainer(c.states, c.gainer, moved_, true)) {
            say("borders the gainer");
            return c.value * k_.treaty_dismantle_ai_border / 100 * near / 100;
        }
        if (c.colonial && !c.interest && gainer_exists && !c.in_colonial_range) {
            say("colony out of colonial range");  // before any adjacency bonus: no colonies across the map
            return 0;
        }
        bool gainer_has_land = gainer_exists || std::any_of(moved_.begin(), moved_.end(), [&](const Moved& m) { return m.gainer == c.gainer; });
        if (!gainer_has_land) {
            // the first state of a country yet to exist: next to other freed land when it can be; its later
            // states must border it (no country in two pieces), so no such bonus below
            if (borders_others(c.states, c.gainer, moved_)) {
                say("first state of a new country, adjacent to land given to another");
                return c.value * k_.treaty_dismantle_ai_adjacent / 100 * near / 100;
            }
            say("first state of a new country");
            return c.value;
        }
        if (c.colonial || c.interest) {
            // across the sea, but on a coastal-only continent only on the coast (an inland one waits for a
            // neighbour the treaty gives the same gainer: the border test above)
            if (c.coastal_only_continent && !c.coastal) {
                say("inland colony on a coastal-only continent");
                return 0;
            }
            if (!c.colonial) {
                say("of interest, across the sea");
                return c.value * near / 100;
            }
            say(c.home_continent ? "colony on the home continent" : "colony across the sea");
            int64_t value = (c.home_continent ? c.value * k_.treaty_dismantle_ai_home_continent / 100 : c.value) * near / 100;
            if (c.coastal_only_continent || c.near_continent) {
                for (const Moved& m : moved_) {  // the more colonies taken on these continents, the less the next is worth
                    value = m.listed_colony ? value * k_.treaty_dismantle_ai_colony_repeat / 100 : value;
                }
            }
            return value;
        }
        say("exclave");
        return c.value * k_.treaty_dismantle_ai_exclave / 100;
    }

    // Why the term is dropped for good, or nullptr.
    const char* rejected(const Candidate& c) const {
        if (spent_ + c.cost > limit_) {
            return "over the budget";
        }
        if (!treaty::term_allowed(action_, c.term)) {
            return "not allowed now";
        }
        if (whole_states(c) > states_left_ || homeland(c) > homeland_left_) {
            return "too much of the loser";
        }
        if (is_new(c) && static_cast<int>(new_countries_.size()) >= k_.treaty_dismantle_ai_max_new) {
            return "too many new countries";
        }
        if (!c.one_province && !c.states.empty() && splits_loser(loser_, moved_, c.states)) {
            return "would leave the loser an enclave or a detached part";
        }
        if (c.infamy > 0 && treaty::infamy_x1000(winner_) + infamy_taken_ + c.infamy > game::badboy_limit_x1000()) {
            return "over BADBOY_LIMIT";
        }
        return nullptr;
    }

    void take(const Candidate& c, const ContinentLists& lists) {
        if (is_new(c)) {
            new_countries_.push_back(c.term.third);
        }
        infamy_taken_ += c.infamy;
        treaty::add_term(action_, c.term);
        spent_ = treaty::points(action_);  // with repeat_cost discounts
        states_left_ -= whole_states(c);
        homeland_left_ -= homeland(c);
        for (uintptr_t s : c.states) {
            std::vector<uintptr_t> one{s};
            bool listed = game::is_colonial_state(s) && (states_on(one, lists.near) || states_on(one, lists.coastal_only));
            moved_.push_back({s, c.one_province ? c.term.province : 0, c.gainer, listed});
        }
    }

    int spent() const { return spent_; }
    int budget() const { return budget_; }
    size_t new_countries() const { return new_countries_.size(); }

private:
    // A single province does not take its state from the loser: it counts for no state.
    static int whole_states(const Candidate& c) { return c.one_province ? 0 : static_cast<int>(c.states.size()); }
    static int homeland(const Candidate& c) {
        return c.one_province ? 0 : static_cast<int>(std::count_if(c.states.begin(), c.states.end(), [](uintptr_t s) { return !game::is_colonial_state(s); }));
    }
    bool is_new(const Candidate& c) const {
        int third = c.term.third;
        return third && !game::country_exists(third) && std::find(new_countries_.begin(), new_countries_.end(), third) == new_countries_.end();
    }

    const Config& k_;
    uintptr_t action_;
    int winner_, loser_, budget_, limit_ = 0, spent_ = 0, states_left_ = 0, homeland_left_ = 0, infamy_taken_ = 0;
    std::vector<Moved> moved_;
    std::vector<int> new_countries_;
};

std::string describe(const Candidate& c) {
    std::string text = GameString::copy(c.term.cb + kCbName);
    if (c.term.third) {
        text += " to " + treaty::tag_name(c.term.third);
    }
    text += " province " + std::to_string(c.term.province) + " (" + std::to_string(c.states.size()) +
            (c.states.size() == 1 ? " state" : " states") + (c.colonial ? ", colonial" : "") + ")";
    return text;
}

}  // namespace

void impose(const Config& k, int winner, int loser, int budget) {
    uintptr_t action = treaty::create(winner, loser);
    if (!action) {
        return;
    }
    treaty::set_budget(action, budget);
    treaty::add_free_terms(action);
    ContinentLists lists = continent_lists(k);
    std::vector<Candidate> all = candidates(k, action, winner, loser, lists);
    Picker picker(k, action, winner, loser, budget);
    std::vector<bool> done(all.size(), false);
    for (;;) {
        size_t best = all.size();
        int64_t best_value = 0;
        for (size_t i = 0; i < all.size(); ++i) {
            const char* why = "";
            int64_t value = done[i] ? 0 : picker.worth(all[i], &why);
            if (value <= 0) {
                continue;
            }
            if (best == all.size()) {
                best = i, best_value = value;
                continue;
            }
            int64_t bc = std::max(all[best].cost, 1), ic = std::max(all[i].cost, 1);
            int64_t lhs = value * bc, rhs = best_value * ic;  // value per point, then the bigger term
            if (lhs > rhs || (lhs == rhs && all[i].cost > all[best].cost)) {
                best = i, best_value = value;
            }
        }
        if (best == all.size()) {
            break;
        }
        done[best] = true;
        const Candidate& c = all[best];
        if (const char* reason = picker.rejected(c)) {
            if (k.treaty_dismantle_log) {
                logf("treaty:   %s dropped: %s", describe(c).c_str(), reason);
            }
            continue;
        }
        if (k.treaty_dismantle_log) {
            const char* why = "";
            int64_t now = picker.worth(c, &why);
            logf("treaty:   %s: cost %d, value %lld -> %lld, %s%s", describe(c).c_str(), c.cost,
                 static_cast<long long>(c.value / 100), static_cast<long long>(now / 100), why, c.interest ? ", of interest" : "");
        }
        picker.take(c, lists);
    }
    logf("treaty: AI %d dismantles %d: %u terms, %d of %d points, %u new countries (%u candidates)", winner, loser,
         static_cast<unsigned>(treaty::goal_count(action)), picker.spent(), picker.budget(),
         static_cast<unsigned>(picker.new_countries()), static_cast<unsigned>(all.size()));
    treaty::show_to_local_human(action, loser);
    treaty::execute(action);
}

}  // namespace se::features::treaty_ai
