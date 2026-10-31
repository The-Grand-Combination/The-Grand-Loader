#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"

namespace se::features {

// treaty: peacetime treaties between two countries. The terms are CBs of cb_types.txt with
// `is_treaty_cb = yes` (their peace options, applied without a war) and a sum of money. The proposal
// is a diplomatic action of its own (CTreatyAction, a CPeaceAction layout with another vtable), so it
// travels, waits in the pending list, is saved and is answered as vanilla actions are. Plan and engine
// facts: docs/re/systems/treaties.md, diplomacy.md.
void install_treaty(const Config& config);

namespace treaty {

// One term: a war goal between the two. The CB, the country that gains (actor) and the one that gives
// (receiver), the third country (0 when the CB has no allowed_countries: the released or liberated
// country, the puppet or sphere member taken) and the target province (0 when it has no allowed_states,
// or all_allowed_states).
struct Term {
    uintptr_t cb = 0;
    int actor = 0;
    int receiver = 0;
    uint32_t province = 0;
    int third = 0;
};

bool enabled();
const std::vector<uintptr_t>& treaty_cbs();  // the CBs with is_treaty_cb, in cb_types.txt order

// A new CTreatyAction from `actor` to `recipient` (game memory; the game deletes it), or 0.
uintptr_t create(int actor, int recipient);
// Appends a term (selected) to the treaty's goal list.
void add_term(uintptr_t action, const Term& term);
// Removes the term at `index`.
void remove_term(uintptr_t action, size_t index);
std::vector<Term> terms(uintptr_t action);
// A forced (dismantle) treaty: its budget in war-score points (0: an ordinary treaty). Forced treaties are
// accepted at once, take only from the recipient, and cost infamy only past the budget.
int budget(uintptr_t action);
// The terms an AI dismantler imposed on a human, in its pending list to be shown (budget -1): already
// applied, never sent.
bool is_notice(uintptr_t action);
// A copy of the treaty (game memory, as the game copies actions).
uintptr_t clone(uintptr_t action);
void set_budget(uintptr_t action, int points);
// What a forced treaty takes, in war-score points (the terms' and the money demanded).
int points(uintptr_t action);
// Infamy (x1000) for `over` points past the budget.
int infamy_past_budget(int over);
// A CB with `is_dismantling_cb = yes`: only these may be terms of a forced treaty.
bool is_dismantling(uintptr_t cb);
// A CB with `dismantle_free = yes`: free in a forced treaty.
bool is_free(uintptr_t cb);
// A CB with `dismantle_infamy = yes`: costs its infamy in a forced treaty too.
bool costs_infamy_when_forced(uintptr_t cb);
// A term's points in a forced treaty: its war score x `dismantle_cost` (0 when free).
int forced_cost(const Term& term);
// The points of the treaty's term at `index` as it counts there: its war score (x dismantle_cost when
// forced), x repeat_cost when an earlier term has the same CB and third country.
int term_points(uintptr_t action, size_t index);
// The infamy (x1000) the dismantle_infamy terms of a forced treaty cost.
int forced_terms_infamy(uintptr_t action);
// Adds the free terms that need no choice (no third country, no state) and can be taken now: a forced
// treaty starts with them.
void add_free_terms(uintptr_t action);
bool is_pending(int country, uintptr_t action);
// Takes `action` out of the country's pending list and deletes it.
void remove_pending(int country, uintptr_t action);
// Money the actor pays the recipient (negative: the recipient pays), in game fixed point (2^15).
int64_t money(uintptr_t action);
void set_money(uintptr_t action, int64_t amount);

// Empty when the treaty can be sent as it is, else the reason (English, for the editor and the log).
std::string problem(uintptr_t action);
// The game's own gate for sending (DiplomaticAction_CanPerform: diplomat travel, war, vassals, ...),
// which ends with problem() being empty.
bool can_send(uintptr_t action);
bool is_human(int country);
// A treaty between the two could be proposed at all (both exist, not the same, not at war).
bool possible(int actor, int recipient);
// The AI's answer if `country` (actor or recipient) were asked: its score; >= 0 accepts.
int ai_score(uintptr_t action, int country);
// Whether the AI would accept (its score, or the yesmen console command).
bool ai_accepts(uintptr_t action, int country);
// War-score points of a term (WarGoal_PeaceCost / 1000).
int term_cost(const Term& term);
// The CB's can_use with `gains` as THIS and `gives` as the target (as CBManager_IsCBUsable scopes it).
bool cb_usable(uintptr_t cb, int gains, int gives);
// What a term of the CB needs, as the war-goal dialog decides it: a third country when allowed_countries
// has conditions; a province when allowed_states has conditions and all_allowed_states is not set (or the
// CB has po_demand_state).
bool cb_needs_country(uintptr_t cb);
// The CB's allowed_countries for `third`, scoped as WarGoal_IsValid does: the scope is the giving
// country, THIS the gaining one, and FROM (kept in the THIS scope) the candidate.
bool country_allowed(uintptr_t cb, int gains, int gives, int third);
// Diagnostics: allowed_countries child by child for `third` (vtable listing VA and result), into the log.
void log_country_conditions(uintptr_t cb, int gains, int gives, int third);
bool cb_needs_province(uintptr_t cb);
// The CB cannot be a treaty term at all (po_annex, po_colony).
bool cb_unsupported(uintptr_t cb);
// Empty when `term` could be added to `action` now (can_use, allowed_states, ownership), else why not.
std::string check_term(uintptr_t action, const Term& term);

// Posts the treaty to the recipient through the network session of `gui_owner` (a CEU3Dialog owner).
void send(uintptr_t action, uintptr_t gui_owner);

}  // namespace treaty

}  // namespace se::features
