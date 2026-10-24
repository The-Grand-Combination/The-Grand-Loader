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

// One term: the CB, the country that gains (actor) and the one that gives (receiver), and the target
// province (0 for CBs without a state target).
struct Term {
    uintptr_t cb = 0;
    int actor = 0;
    int receiver = 0;
    uint32_t province = 0;
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
// Money the actor pays the recipient (negative: the recipient pays), in game fixed point (2^15).
int64_t money(uintptr_t action);
void set_money(uintptr_t action, int64_t amount);

// Empty when the treaty can be sent as it is, else the reason (English, for the editor and the log).
std::string problem(uintptr_t action);
// The AI's answer if `country` (actor or recipient) were asked: its score; >= 0 accepts.
int ai_score(uintptr_t action, int country);
// War-score points of a term (WarGoal_PeaceCost / 1000).
int term_cost(const Term& term);
// The CB's can_use with `gains` as THIS and `gives` as the target (as CBManager_IsCBUsable scopes it).
bool cb_usable(uintptr_t cb, int gains, int gives);
// The CB takes a state or province (po_demand_state); terms of other CBs have province 0.
bool cb_needs_province(uintptr_t cb);
// The CB cannot be a treaty term at all (po_annex, po_transfer_provinces).
bool cb_unsupported(uintptr_t cb);
// Empty when `term` could be added to `action` now (can_use, allowed_states, ownership), else why not.
std::string check_term(uintptr_t action, const Term& term);

// Posts the treaty to the recipient through the network session of `gui_owner` (a CEU3Dialog owner).
void send(uintptr_t action, uintptr_t gui_owner);

}  // namespace treaty

}  // namespace se::features
