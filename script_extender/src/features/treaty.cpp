#include "features/treaty.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "features/treaty_ui.h"
#include "game/localisation.h"
#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Treaties: docs/re/systems/treaties.md (plan), diplomacy.md (diplomatic actions, peace offers).
//
// - Keywords: right before CBManager_Load, `is_treaty_cb`, `treaty_action` (the action's type id) and
//   `se_treaty_money` (its money field in saves and commands) become dynamic lexer tokens.
//   CCasusBelliType's OnKeyword (vtable slot 4) gets a wrapper that marks a CB for `is_treaty_cb = yes`.
//   Both hooks chain to whatever is installed before them (demand_province uses the same two sites).
// - CTreatyAction: a CPeaceAction object (0x98 bytes: base action, CPeaceOffer at +0x28) plus an i64 at
//   +0x98, with a vtable copied from CPeaceAction's in which every slot that reads the war is replaced.
//   The terms are CWarGoals in the offer's war_goal vector (all selected), each with its own actor (who
//   gains) and receiver (who gives). DiplomaticAction_CreateByType gets an entry hook that builds one
//   for the `treaty_action` token: that covers multiplayer commands, saves and the diplomacy view.
// - The recipient: a human gets the vanilla DefaultDialog box (base slot 13) with our title and text; the
//   host's AI answers in AI_ProcessPendingProposals, where a hook on the type switch gives our chance.
// - Acceptance (slot 32, on every machine): every term is checked again and run with WarGoal_Execute,
//   the money moves, and both countries are repaired as after a peace.

namespace se::features {

namespace {

using game::GameString;
using game::read;

// Lexer, CB loading, CBs
constexpr uint32_t kTokenAddDynamic = 0x009902D0;  // cdecl (const std::string* name, bool allowExisting) -> id
constexpr uint32_t kLexerReady = 0x01258A64;
constexpr uint32_t kCbManagerLoadCall = 0x0063DCD2;
constexpr uint32_t kCbOnKeywordSlot = 0x00E33734;  // CCasusBelliType_vftable slot 4
constexpr uint32_t kCbHasPeaceOption = 0x0090A800;  // EAX = cb, ECX = token -> AL
constexpr int kTokenYes = 0x29F;
constexpr int kPoAnnex = 0x8A1;
constexpr uint32_t kParserValueToken = 0x228;  // token of the value just read
constexpr uint32_t kParserValueText = 0x22C;   // its text
constexpr uint32_t kCbName = 0x14;             // std::string
constexpr uint32_t kCbCanUse = 0x20;           // vt slot: bool (CEventScope*)
constexpr int kPoDemandState = 0x8A3;
constexpr int kPoTransferProvinces = 0x8A7;
constexpr uint32_t kEventScopeCtor = 0x008A8650;  // thiscall (parent)
constexpr uint32_t kEventScopeSize = 0x48;
constexpr uint32_t kScopeCountryTag = 0x08;
constexpr uint32_t kScopeCountryIndex = 0x0C;
constexpr uint32_t kScopeThis = 0x24;

// Diplomatic actions
constexpr uint32_t kCreateByType = 0x0090E850;  // cdecl (int token) -> action
constexpr uint8_t kCreateByTypeEntry[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr uint32_t kPeaceVtable = 0x00E34474;
constexpr uint32_t kBaseActionVtable = 0x00E3405C;
constexpr uint32_t kPeaceCtorDefault = 0x00916A30;  // stdcall (this, date, actorTag, actorIdx, recTag, recIdx)
constexpr uint32_t kPeaceCopyCtor = 0x00916B50;     // stdcall (this, src)
constexpr uint32_t kPeaceWriteFields = 0x00919660;  // thiscall (stream)
constexpr uint32_t kPeaceOnKeyword = 0x00919690;    // thiscall (parser, token)
constexpr uint32_t kNullDate = 0x01259ABC;
constexpr uint32_t kActionSize = 0xA0;
constexpr uint32_t kSlotCount = 38;
// CDiplomaticAction / CPeaceAction fields
constexpr uint32_t kActorTag = 0x08;
constexpr uint32_t kActorIndex = 0x0C;
constexpr uint32_t kRecipientIndex = 0x14;
constexpr uint32_t kFlags = 0x44;   // offer+0x1C: vector<int>, parallel to the goals
constexpr uint32_t kGoals = 0x64;   // offer+0x3C: vector<CWarGoal> (0x40 each)
constexpr uint32_t kMoney = 0x98;   // ours: i64, actor -> recipient
// The AI's pending proposals: the type switch
constexpr uint32_t kAiTypeSwitch = 0x00820EBB;  // MOV EAX,[ESI]; MOV EDX,[EAX+0x20]; MOV ECX,ESI
constexpr uint8_t kAiTypeSwitchBytes[] = {0x8B, 0x06, 0x8B, 0x50, 0x20, 0x8B, 0xCE};
constexpr uint32_t kAiChanceDone = 0x0082102B;  // CMP EAX, 0x32: the chance decides

// War goals
constexpr uint32_t kWarGoalSize = 0x40;
constexpr uint32_t kWarGoalCtor = 0x0094BDB0;     // EAX = this, ECX = cb, (actorTag, actorIdx, recTag, recIdx)
constexpr uint32_t kWarGoalExecute = 0x0094E660;  // stdcall (goal)
constexpr uint32_t kWarGoalPeaceCost = 0x00951920;  // stdcall (goal, int* out, bool)
constexpr uint32_t kWarGoalInfamy = 0x009500E0;     // EDI = int* out, stdcall (goal)
constexpr uint32_t kWarGoalIsValid = 0x00952850;    // thiscall ECX = goal, (war, crisis, 0) -> AL
constexpr uint32_t kGoalCb = 0x0C;
constexpr uint32_t kGoalActorIndex = 0x1C;
constexpr uint32_t kGoalReceiverIndex = 0x24;
constexpr uint32_t kGoalProvince = 0x28;

// Countries, provinces, world
constexpr uint32_t kCountryTag = 0x1C;
constexpr uint32_t kCountryIndex = 0x20;
constexpr uint32_t kCountryAi = 0x364;
constexpr uint32_t kCountryArmies = 0x7B4;
constexpr uint32_t kCountryProvinces = 0x9D8;  // vector<int>; empty = the country does not exist
constexpr uint32_t kCountryControlled = 0x9E8;  // list {id, ?, next}
constexpr uint32_t kCountryRelations = 0xBE8;
constexpr uint32_t kCountryStateCount = 0xE4C;
constexpr uint32_t kCountryTreasury = 0xE78;   // i64, 2^15 fixed point
constexpr uint32_t kCountryInfamy = 0x1430;    // x1000
constexpr uint32_t kRelationWar = 0x34;
constexpr uint32_t kRelationValue = 0x3C;
constexpr uint32_t kRelationMax = 0x0131B1E8;
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kProvinceController = 0x134;
constexpr uint32_t kWorldHumans = 0xAEC;    // vector<int>: 0 = AI
constexpr uint32_t kWorldMilitary = 0xAB4;
constexpr uint32_t kWorldRunning = 0xD1C;
constexpr uint32_t kValidateCapital = 0x0050E540;      // stdcall (country)
constexpr uint32_t kRepositionArmies = 0x005B53D0;     // stdcall (world+0xAB4, country)
constexpr uint32_t kUpdatePortBlockade = 0x004B7C50;   // EDI = province
constexpr uint32_t kUpdateHostileFlags = 0x005DBD90;   // EAX = unit list
constexpr uint32_t kCalculateDistribution = 0x00520150;  // stdcall (country)
constexpr uint32_t kAiDailyUpdate = 0x0085CCB0;        // EAX = country + 0x364

// Game memory and strings
constexpr uint32_t kOperatorNew = 0x00AAE9AF;     // cdecl (size)
constexpr uint32_t kOperatorDelete = 0x00AAE91B;  // cdecl (p)
constexpr uint32_t kStringAssign = 0x00409350;    // thiscall (const char*, length)
constexpr uint32_t kWriteToken = 0x009A0940;      // ECX = token, stdcall (stream)
constexpr uint32_t kWriteString = 0x0099F770;     // thiscall ECX = std::string*, (stream, 0)
constexpr uint32_t kTermsText = 0x00929480;       // thiscall ECX = offer, (std::string* out)
// Command queue: gui owner vt[0x44]() -> +0x38 session, vt[0x18](command)
constexpr uint32_t kOwnerSessionOwner = 0x44;
constexpr uint32_t kSessionOwnerSession = 0x38;
constexpr uint32_t kSessionQueueCommand = 0x18;

constexpr const char* kKeyword = "is_treaty_cb";
constexpr const char* kActionKeyword = "treaty_action";
constexpr const char* kMoneyKeyword = "se_treaty_money";
constexpr int64_t kMoneyOne = 32768;  // 1.0 in the treasury's fixed point

Config g_config;
int g_token = 0;         // is_treaty_cb
int g_action_token = 0;  // treaty_action
int g_money_token = 0;   // se_treaty_money
std::vector<uintptr_t> g_cbs;
uintptr_t g_prev_load = 0;        // what the CBManager_Load call called before us
uintptr_t g_prev_on_keyword = 0;  // CCasusBelliType slot 4 before us
uintptr_t g_create_trampoline = 0;
uintptr_t g_ai_back = 0;  // kAiTypeSwitch + 7
uintptr_t g_ai_done = 0;
uintptr_t g_vtable_storage[kSlotCount + 1];  // [0] = RTTI locator, then the slots
uintptr_t g_vtable = 0;                      // &g_vtable_storage[1]

template <class T>
T fn(uint32_t va) {
    return reinterpret_cast<T>(hooks::exe_va(va));
}

void* game_new(size_t size) { return fn<void*(__cdecl*)(size_t)>(kOperatorNew)(size); }
void game_delete(void* p) { fn<void(__cdecl*)(void*)>(kOperatorDelete)(p); }

// A game std::string at `out` (uninitialised memory) holding `text`; the game frees it.
void make_game_string(uintptr_t out, const std::string& text) {
    std::memset(reinterpret_cast<void*>(out), 0, 0x1C);
    *reinterpret_cast<uint32_t*>(out + 0x14) = 15;
    using AssignFn = void(__fastcall*)(uintptr_t, void*, const char*, size_t);
    fn<AssignFn>(kStringAssign)(out, nullptr, text.c_str(), text.size());
}

// Copies and frees a game std::string the game wrote into `at`.
std::string take_game_string(uintptr_t at) {
    std::string text = GameString::copy(at);
    if (read<uint32_t>(at + 0x14) > 15) {
        game_delete(read<void*>(at));
    }
    return text;
}

bool is_treaty(uintptr_t action) { return action && read<uintptr_t>(action) == g_vtable; }

bool is_treaty_cb(uintptr_t cb) {
    for (uintptr_t c : g_cbs) {
        if (c == cb) {
            return true;
        }
    }
    return false;
}

uintptr_t relation(int a, int b) {
    return read<uintptr_t>(read<uintptr_t>(game::country(a) + kCountryRelations) + static_cast<uint32_t>(b) * 4);
}

bool exists(int country) {
    uintptr_t c = game::country(country);
    return c && read<uintptr_t>(c + kCountryProvinces) != read<uintptr_t>(c + kCountryProvinces + 4);
}

bool is_human(int country) {
    uintptr_t humans = game::world() + kWorldHumans;
    auto begin = read<const int*>(humans);
    auto end = read<const int*>(humans + 4);
    return country >= 0 && country < end - begin && begin[country] != 0;
}

// --- Texts ---------------------------------------------------------------------------------------

const char* const kTextKeys[] = {
    "SE_TREATY_TITLE",         "SE_TREATY_OFFER",        "SE_TREATY_MONEY_ACTOR",  "SE_TREATY_MONEY_RECIPIENT",
    "SE_TREATY_NO_TERMS",      "SE_TREATY_AT_WAR",       "SE_TREATY_GONE",         "SE_TREATY_TERM_INVALID",
    "SE_TREATY_TERM_ANNEX",    "SE_TREATY_TERM_OWNER",   "SE_TREATY_TERM_OCCUPIED", "SE_TREATY_TERM_LAST_STATE",
    "SE_TREATY_MONEY_SHORT",   "SE_TREATY_TERM_SIDES",
};

std::once_flag g_texts_once;
std::map<std::string, std::string> g_texts;

// The English text of `key` (mod/script_extender/localisation), or the key itself.
std::string text(const char* key) {
    std::call_once(g_texts_once, [] { g_texts = game::read_localisation({std::begin(kTextKeys), std::end(kTextKeys)}); });
    auto it = g_texts.find(key);
    return it == g_texts.end() ? std::string(key) : it->second;
}

std::string replace(std::string s, const std::string& token, const std::string& value) {
    for (size_t at = s.find(token); at != std::string::npos; at = s.find(token, at + value.size())) {
        s.replace(at, token.size(), value);
    }
    return s;
}

std::string pounds(int64_t fixed) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(fixed / kMoneyOne));
    return buf;
}

// --- Terms ---------------------------------------------------------------------------------------

struct Vec {
    uintptr_t begin, end, capacity;
};

Vec vec(uintptr_t at) { return {read<uintptr_t>(at), read<uintptr_t>(at + 4), read<uintptr_t>(at + 8)}; }

size_t goal_count(uintptr_t action) {
    Vec goals = vec(action + kGoals);
    return (goals.end - goals.begin) / kWarGoalSize;
}

uintptr_t goal(uintptr_t action, size_t i) { return vec(action + kGoals).begin + i * kWarGoalSize; }

bool selected(uintptr_t action, size_t i) {
    Vec flags = vec(action + kFlags);
    return i < (flags.end - flags.begin) / 4 && read<int>(flags.begin + i * 4) != 0;
}

// Replaces a game vector's storage with `count` elements of `size` bytes copied from `data`.
void set_vector(uintptr_t at, const void* data, size_t count, size_t size) {
    Vec old = vec(at);
    void* storage = count ? game_new(count * size) : nullptr;
    if (count) {
        std::memcpy(storage, data, count * size);
    }
    auto* v = reinterpret_cast<uintptr_t*>(at);
    v[0] = reinterpret_cast<uintptr_t>(storage);
    v[1] = v[2] = v[0] + count * size;
    if (old.begin) {
        game_delete(reinterpret_cast<void*>(old.begin));
    }
}

int peace_cost(uintptr_t goal_ptr) {
    int out = 0;
    using CostFn = int*(__stdcall*)(uintptr_t, int*, bool);
    fn<CostFn>(kWarGoalPeaceCost)(goal_ptr, &out, false);
    return out;
}

int infamy(uintptr_t goal_ptr) {
    int out = 0;
    int* out_ptr = &out;
    uintptr_t f = hooks::exe_va(kWarGoalInfamy);
    __asm {
        push edi
        mov edi, out_ptr
        push goal_ptr
        call f
        pop edi
    }
    return out;
}

bool goal_valid(uintptr_t goal_ptr) {
    using ValidFn = bool(__fastcall*)(uintptr_t, void*, uintptr_t, bool, int);
    return fn<ValidFn>(kWarGoalIsValid)(goal_ptr, nullptr, 0, false, 0);
}

bool has_peace_option(uintptr_t cb, int token) {
    uintptr_t f = hooks::exe_va(kCbHasPeaceOption);
    bool result;
    __asm {
        mov eax, cb
        mov ecx, token
        call f
        mov result, al
    }
    return result;
}

// Builds a CWarGoal in `out` (0x40 bytes).
void build_goal(void* out, const treaty::Term& term) {
    uintptr_t actor = game::country(term.actor);
    uintptr_t receiver = game::country(term.receiver);
    uint32_t actor_tag = read<uint32_t>(actor + kCountryTag);
    uint32_t receiver_tag = read<uint32_t>(receiver + kCountryTag);
    int actor_index = term.actor;
    int receiver_index = term.receiver;
    uintptr_t cb = term.cb;
    void* goal_memory = out;
    uintptr_t f = hooks::exe_va(kWarGoalCtor);
    __asm {
        push receiver_index
        push receiver_tag
        push actor_index
        push actor_tag
        mov eax, goal_memory
        mov ecx, cb
        call f
    }
    *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(out) + kGoalProvince) = term.province;
}

// Empty when the term at `g` can be applied between the treaty's two countries now, else why not.
std::string term_problem(uintptr_t action, uintptr_t g) {
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    uintptr_t cb = read<uintptr_t>(g + kGoalCb);
    int gains = read<int>(g + kGoalActorIndex);
    int gives = read<int>(g + kGoalReceiverIndex);
    if (!cb || !is_treaty_cb(cb) || !goal_valid(g)) {
        return text("SE_TREATY_TERM_INVALID");
    }
    if (!((gains == a && gives == r) || (gains == r && gives == a))) {
        return text("SE_TREATY_TERM_SIDES");
    }
    if (has_peace_option(cb, kPoAnnex) || has_peace_option(cb, kPoTransferProvinces)) {
        return text("SE_TREATY_TERM_ANNEX");
    }
    uint32_t id = read<uint32_t>(g + kGoalProvince);
    if (id) {
        uintptr_t province = game::province(id);
        if (read<int>(province + kProvinceOwner) != gives) {
            return text("SE_TREATY_TERM_OWNER");
        }
        if (read<int>(province + kProvinceController) != gives) {
            return text("SE_TREATY_TERM_OCCUPIED");
        }
        if (read<int>(game::country(gives) + kCountryStateCount) <= 1) {
            return text("SE_TREATY_TERM_LAST_STATE");
        }
    }
    return {};
}

// --- The treaty as a whole -----------------------------------------------------------------------

std::string treaty_problem(uintptr_t action) {
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    if (!exists(a) || !exists(r) || a == r) {
        return text("SE_TREATY_GONE");
    }
    if (read<uintptr_t>(relation(a, r) + kRelationWar)) {
        return text("SE_TREATY_AT_WAR");
    }
    int64_t m = read<int64_t>(action + kMoney);
    size_t count = 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        if (!selected(action, i)) {
            continue;
        }
        ++count;
        std::string why = term_problem(action, goal(action, i));
        if (!why.empty()) {
            return why;
        }
    }
    if (count == 0 && m == 0) {
        return text("SE_TREATY_NO_TERMS");
    }
    int payer = m > 0 ? a : r;
    if (m != 0 && read<int64_t>(game::country(payer) + kCountryTreasury) < (m > 0 ? m : -m)) {
        return text("SE_TREATY_MONEY_SHORT");
    }
    return {};
}

// The AI's view of the treaty as `self`: what it gains minus a weighted price of what it gives, plus
// its relation with the other side, minus a base reluctance. Reads synced state only.
int score_for(uintptr_t action, int self) {
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    int other = self == a ? r : a;
    int64_t gain = 0;  // war-score points x 100
    int64_t give = 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        if (!selected(action, i)) {
            continue;
        }
        uintptr_t g = goal(action, i);
        int64_t cost = peace_cost(g) / 10;  // x1000 -> x100
        if (read<int>(g + kGoalActorIndex) == self) {
            gain += cost;
        } else if (read<int>(g + kGoalReceiverIndex) == self) {
            give += cost;
        }
    }
    int64_t m = read<int64_t>(action + kMoney);  // actor -> recipient
    int64_t to_self = self == r ? m : -m;
    int64_t per_point = static_cast<int64_t>(g_config.treaty_ai_money_per_point > 0 ? g_config.treaty_ai_money_per_point : 1);
    int64_t money_points = to_self * 100 / (per_point * kMoneyOne);
    if (money_points > 0) {
        gain += money_points;
    } else {
        give -= money_points;
    }
    int value = read<int>(relation(self, other) + kRelationValue);
    int max = read<int>(hooks::exe_va(kRelationMax));
    int64_t relation_term = max > 0 ? static_cast<int64_t>(value) * g_config.treaty_ai_relation * 100 / max : 0;
    int64_t total = gain - give * g_config.treaty_ai_give_weight / 100 + relation_term -
                    static_cast<int64_t>(g_config.treaty_ai_reluctance) * 100;
    return static_cast<int>(total);
}

// --- CTreatyAction slots -------------------------------------------------------------------------

uintptr_t new_treaty() {
    void* memory = game_new(kActionSize);
    if (!memory) {
        return 0;
    }
    using CtorFn = uintptr_t(__stdcall*)(void*, uint32_t, uint32_t, int, uint32_t, int);
    fn<CtorFn>(kPeaceCtorDefault)(memory, read<uint32_t>(hooks::exe_va(kNullDate)), 0x2D2D2D, 0, 0x2D2D2D, 0);
    auto action = reinterpret_cast<uintptr_t>(memory);
    *reinterpret_cast<uintptr_t*>(action) = g_vtable;
    *reinterpret_cast<int64_t*>(action + kMoney) = 0;
    return action;
}

uintptr_t __fastcall slot_clone(uintptr_t self, void*) {
    void* memory = game_new(kActionSize);
    if (!memory) {
        return 0;
    }
    using CopyFn = uintptr_t(__stdcall*)(void*, uintptr_t);
    fn<CopyFn>(kPeaceCopyCtor)(memory, self);
    auto copy = reinterpret_cast<uintptr_t>(memory);
    *reinterpret_cast<uintptr_t*>(copy) = g_vtable;
    *reinterpret_cast<int64_t*>(copy + kMoney) = read<int64_t>(self + kMoney);
    return copy;
}

int __fastcall slot_type_id(uintptr_t, void*) { return g_action_token; }

uintptr_t __fastcall slot_name(uintptr_t, void*, uintptr_t out) {
    make_game_string(out, "SE_TREATY");
    return out;
}

uintptr_t __fastcall slot_title(uintptr_t, void*, uintptr_t out) {
    make_game_string(out, text("SE_TREATY_TITLE"));
    return out;
}

// The recipient's box: the terms, one bullet each (the peace offer's text), and the money.
uintptr_t __fastcall slot_offer_text(uintptr_t self, void*, uintptr_t out) {
    std::string body = text("SE_TREATY_OFFER");
    uint8_t terms[0x1C];
    using TermsFn = void(__fastcall*)(uintptr_t, void*, void*);
    fn<TermsFn>(kTermsText)(self + 0x28, nullptr, terms);
    body += "\n" + take_game_string(reinterpret_cast<uintptr_t>(terms));
    int64_t m = read<int64_t>(self + kMoney);
    if (m != 0) {
        body += "\n" + replace(text(m > 0 ? "SE_TREATY_MONEY_ACTOR" : "SE_TREATY_MONEY_RECIPIENT"), "$MONEY$",
                               pounds(m > 0 ? m : -m));
    }
    make_game_string(out, body);
    return out;
}

bool __fastcall slot_can_perform(uintptr_t self, void*, bool) { return treaty_problem(self).empty(); }

void __fastcall slot_execute(uintptr_t self, void*, void*) {
    int a = read<int>(self + kActorIndex);
    int r = read<int>(self + kRecipientIndex);
    std::string why = treaty_problem(self);
    if (!why.empty()) {
        logf("treaty: %d -> %d accepted but not applied: %s", a, r, why.c_str());
        return;
    }
    size_t applied = 0;
    for (size_t i = 0, n = goal_count(self); i < n; ++i) {
        if (!selected(self, i)) {
            continue;
        }
        uintptr_t g = goal(self, i);
        std::string term_why = term_problem(self, g);  // an earlier term may have changed the map
        if (!term_why.empty()) {
            logf("treaty: term %u skipped: %s", static_cast<unsigned>(i), term_why.c_str());
            continue;
        }
        if (g_config.treaty_infamy) {
            uintptr_t gainer = game::country(read<int>(g + kGoalActorIndex));
            *reinterpret_cast<int*>(gainer + kCountryInfamy) += infamy(g);
        }
        fn<void(__stdcall*)(uintptr_t)>(kWarGoalExecute)(g);
        ++applied;
    }
    int64_t m = read<int64_t>(self + kMoney);
    if (m != 0) {
        uintptr_t payer = game::country(m > 0 ? a : r);
        uintptr_t payee = game::country(m > 0 ? r : a);
        int64_t amount = m > 0 ? m : -m;
        *reinterpret_cast<int64_t*>(payer + kCountryTreasury) -= amount;
        *reinterpret_cast<int64_t*>(payee + kCountryTreasury) += amount;
    }
    // The repairs a peace runs for its participants (CPeaceAction_Execute, 0x009183F8).
    uintptr_t world = game::world();
    for (int index : {a, r}) {
        uintptr_t c = game::country(index);
        fn<void(__stdcall*)(uintptr_t)>(kValidateCapital)(c);
        if (read<uint8_t>(world + kWorldRunning)) {
            fn<void(__stdcall*)(uintptr_t, uintptr_t)>(kRepositionArmies)(world + kWorldMilitary, c);
            uintptr_t blockade = hooks::exe_va(kUpdatePortBlockade);
            for (uintptr_t node = read<uintptr_t>(c + kCountryControlled); node; node = read<uintptr_t>(node + 8)) {
                uintptr_t province = game::province(read<uint32_t>(node));
                __asm {
                    push edi
                    mov edi, province
                    call blockade
                    pop edi
                }
            }
            uintptr_t units = c + kCountryArmies;
            uintptr_t hostile = hooks::exe_va(kUpdateHostileFlags);
            __asm {
                mov eax, units
                call hostile
            }
        }
        if (applied) {
            fn<void(__stdcall*)(uintptr_t)>(kCalculateDistribution)(c);
        }
        uintptr_t ai = c + kCountryAi;
        uintptr_t daily = hooks::exe_va(kAiDailyUpdate);
        __asm {
            mov eax, ai
            call daily
        }
    }
    logf("treaty: %d -> %d applied (%u terms, money %lld)", a, r, static_cast<unsigned>(applied),
         static_cast<long long>(m / kMoneyOne));
}

void __fastcall slot_write_fields(uintptr_t self, void*, uintptr_t stream) {
    using WriteFn = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    fn<WriteFn>(kPeaceWriteFields)(self, nullptr, stream);
    int64_t m = read<int64_t>(self + kMoney);
    if (m == 0) {
        return;
    }
    uintptr_t write_token = hooks::exe_va(kWriteToken);
    int token = g_money_token;
    __asm {
        push stream
        mov ecx, token
        call write_token
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(m));
    std::string value = buf;
    GameString game_value(value);
    using StringFn = void(__fastcall*)(const GameString*, void*, uintptr_t, int);
    fn<StringFn>(kWriteString)(&game_value, nullptr, stream, 0);
}

void __fastcall slot_on_keyword(uintptr_t self, void*, uintptr_t parser, int token) {
    if (g_money_token && token == g_money_token) {
        const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
        *reinterpret_cast<int64_t*>(self + kMoney) = std::strtoll(value, nullptr, 10);
        return;
    }
    using KeyFn = void(__fastcall*)(uintptr_t, void*, uintptr_t, int);
    fn<KeyFn>(kPeaceOnKeyword)(self, nullptr, parser, token);
}

// The chance AI_ProcessPendingProposals rolls against (0 or 100).
int __stdcall ai_chance(uintptr_t action) {
    int r = read<int>(action + kRecipientIndex);
    std::string why = treaty_problem(action);
    int score = why.empty() ? score_for(action, r) : -1;
    logf("treaty: AI %d answers %d: score %d%s%s", r, read<int>(action + kActorIndex), score,
         why.empty() ? "" : ", ", why.c_str());
    return score >= 0 ? 100 : 0;
}

// Jumped to from kAiTypeSwitch, ESI = the pending action.
__declspec(naked) void ai_thunk() {
    __asm {
        mov eax, [esi]
        cmp eax, g_vtable
        jne vanilla
        SE_SAVE_ALL
        push esi
        call ai_chance
        mov [esp + SE_SAVED_EAX], eax
        SE_RESTORE_ALL
        jmp g_ai_done
    vanilla:
        mov edx, [eax + 0x20]
        mov ecx, esi
        jmp g_ai_back
    }
}

uintptr_t __cdecl create_by_type(int token) {
    if (g_action_token && token == g_action_token) {
        return new_treaty();
    }
    return reinterpret_cast<uintptr_t(__cdecl*)(int)>(g_create_trampoline)(token);
}

// --- Keywords ------------------------------------------------------------------------------------

int add_token(const char* name) {
    std::string s = name;
    GameString game_name(s);
    return fn<int(__cdecl*)(const GameString*, bool)>(kTokenAddDynamic)(&game_name, true);
}

void __stdcall register_tokens() {
    if (g_token) {
        return;
    }
    if (!read<uint8_t>(hooks::exe_va(kLexerReady))) {
        logf("treaty: the lexer is not ready before cb_types.txt; treaties stay off");
        return;
    }
    g_token = add_token(kKeyword);
    g_action_token = add_token(kActionKeyword);
    g_money_token = add_token(kMoneyKeyword);
    logf("treaty: tokens %s 0x%X, %s 0x%X, %s 0x%X", kKeyword, g_token, kActionKeyword, g_action_token,
         kMoneyKeyword, g_money_token);
}

__declspec(naked) void load_thunk() {
    __asm {
        SE_SAVE_ALL
        call register_tokens
        SE_RESTORE_ALL
        jmp g_prev_load
    }
}

using OnKeywordFn = void(__fastcall*)(uintptr_t cb, void*, uintptr_t parser, int token);

void __fastcall on_keyword(uintptr_t cb, void*, uintptr_t parser, int token) {
    if (!g_token || token != g_token) {
        reinterpret_cast<OnKeywordFn>(g_prev_on_keyword)(cb, nullptr, parser, token);
        return;
    }
    if (read<int>(parser + kParserValueToken) == kTokenYes && !is_treaty_cb(cb)) {
        g_cbs.push_back(cb);
        logf("treaty: %s is a treaty CB", GameString::copy(cb + kCbName).c_str());
    }
}

// --- Install -------------------------------------------------------------------------------------

void build_vtable() {
    uintptr_t peace = hooks::exe_va(kPeaceVtable);
    // RTTI of the base class: a cast to CPeaceAction must not take a treaty for a peace offer.
    g_vtable_storage[0] = read<uintptr_t>(hooks::exe_va(kBaseActionVtable) - 4);
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        g_vtable_storage[i + 1] = read<uintptr_t>(peace + i * 4);
    }
    g_vtable = reinterpret_cast<uintptr_t>(&g_vtable_storage[1]);
    auto set = [](uint32_t slot, uintptr_t f) { g_vtable_storage[slot + 1] = f; };
    auto set_exe = [&](uint32_t slot, uint32_t va) { set(slot, hooks::exe_va(va)); };
    set(2, reinterpret_cast<uintptr_t>(&slot_write_fields));
    set(4, reinterpret_cast<uintptr_t>(&slot_on_keyword));
    set(7, reinterpret_cast<uintptr_t>(&slot_title));
    set(8, reinterpret_cast<uintptr_t>(&slot_type_id));
    set(9, reinterpret_cast<uintptr_t>(&slot_name));
    set(12, reinterpret_cast<uintptr_t>(&slot_clone));
    set_exe(13, 0x00913020);  // DiplomaticAction_ShowIncoming: the DefaultDialog box
    set_exe(14, 0x00912E70);  // DiplomaticAction_CreateDialog (never reached: not in the view's table)
    set(16, reinterpret_cast<uintptr_t>(&slot_offer_text));
    set_exe(17, 0x00633620);  // no third parties
    set_exe(18, 0x0092C720);  // SetRecipient: the base's, no war lookup
    set_exe(19, 0x0092C700);  // SetActor
    for (uint32_t slot = 20; slot <= 23; ++slot) {
        set_exe(slot, 0x00424B40);  // no diplomatic or influence cost, no relation change
    }
    set_exe(26, 0x00A31490);  // not while at war with the recipient
    set_exe(27, 0x0058CFD0);  // never auto-accepted
    set_exe(28, 0x00A31490);  // diplomat travel time
    set(32, reinterpret_cast<uintptr_t>(&slot_execute));
    set_exe(34, 0x00A46A60);  // declined: nothing
    set(36, reinterpret_cast<uintptr_t>(&slot_can_perform));
    set_exe(37, 0x00928300);  // no "cannot send" text
}

// The target of the `CALL rel32` at `site`.
uintptr_t call_target(uintptr_t site) { return site + 5 + read<int32_t>(site + 1); }

}  // namespace

namespace treaty {

bool enabled() { return g_vtable != 0 && g_action_token != 0; }

const std::vector<uintptr_t>& treaty_cbs() { return g_cbs; }

uintptr_t create(int actor, int recipient) {
    uintptr_t action = enabled() ? new_treaty() : 0;
    if (!action) {
        return 0;
    }
    uintptr_t a = game::country(actor);
    uintptr_t r = game::country(recipient);
    using SetFn = void(__fastcall*)(uintptr_t, void*, uint32_t, int);
    reinterpret_cast<SetFn>(hooks::exe_va(0x0092C700))(action, nullptr, read<uint32_t>(a + kCountryTag), actor);
    reinterpret_cast<SetFn>(hooks::exe_va(0x0092C720))(action, nullptr, read<uint32_t>(r + kCountryTag), recipient);
    *reinterpret_cast<uint32_t*>(action + 0x18) = read<uint32_t>(game::world() + 0xB0C);
    return action;
}

void add_term(uintptr_t action, const Term& term) {
    size_t n = goal_count(action);
    std::vector<uint8_t> goals((n + 1) * kWarGoalSize);
    if (n) {
        std::memcpy(goals.data(), reinterpret_cast<void*>(goal(action, 0)), n * kWarGoalSize);
    }
    build_goal(goals.data() + n * kWarGoalSize, term);
    std::vector<int> flags(n + 1, 1);
    set_vector(action + kGoals, goals.data(), n + 1, kWarGoalSize);
    set_vector(action + kFlags, flags.data(), n + 1, 4);
}

void remove_term(uintptr_t action, size_t index) {
    size_t n = goal_count(action);
    if (index >= n) {
        return;
    }
    std::vector<uint8_t> goals(n * kWarGoalSize);
    std::memcpy(goals.data(), reinterpret_cast<void*>(goal(action, 0)), n * kWarGoalSize);
    goals.erase(goals.begin() + index * kWarGoalSize, goals.begin() + (index + 1) * kWarGoalSize);
    std::vector<int> flags(n - 1, 1);
    set_vector(action + kGoals, goals.data(), n - 1, kWarGoalSize);
    set_vector(action + kFlags, flags.data(), n - 1, 4);
}

std::vector<Term> terms(uintptr_t action) {
    std::vector<Term> out;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        uintptr_t g = goal(action, i);
        out.push_back({read<uintptr_t>(g + kGoalCb), read<int>(g + kGoalActorIndex), read<int>(g + kGoalReceiverIndex),
                       read<uint32_t>(g + kGoalProvince)});
    }
    return out;
}

int64_t money(uintptr_t action) { return read<int64_t>(action + kMoney); }

void set_money(uintptr_t action, int64_t amount) { *reinterpret_cast<int64_t*>(action + kMoney) = amount; }

std::string problem(uintptr_t action) { return treaty_problem(action); }

int ai_score(uintptr_t action, int country) { return score_for(action, country); }

int term_cost(const Term& term) {
    uint8_t g[kWarGoalSize];
    build_goal(g, term);
    return peace_cost(reinterpret_cast<uintptr_t>(g)) / 1000;
}

bool cb_usable(uintptr_t cb, int gains, int gives) {
    alignas(8) uint8_t outer[kEventScopeSize];
    alignas(8) uint8_t inner[kEventScopeSize];
    using ScopeCtorFn = void(__fastcall*)(void*, void*, void*);
    fn<ScopeCtorFn>(kEventScopeCtor)(outer, nullptr, nullptr);
    fn<ScopeCtorFn>(kEventScopeCtor)(inner, nullptr, nullptr);
    auto set_country = [](uint8_t* scope, int index) {
        *reinterpret_cast<uint32_t*>(scope + kScopeCountryTag) = read<uint32_t>(game::country(index) + kCountryTag);
        *reinterpret_cast<int*>(scope + kScopeCountryIndex) = index;
    };
    set_country(outer, gains);
    set_country(inner, gives);
    *reinterpret_cast<void**>(outer + kScopeThis) = inner;
    using CanUseFn = bool(__fastcall*)(uintptr_t, void*, void*);
    return read<CanUseFn>(read<uintptr_t>(cb) + kCbCanUse)(cb, nullptr, outer);
}

bool cb_needs_province(uintptr_t cb) { return has_peace_option(cb, kPoDemandState); }

bool cb_unsupported(uintptr_t cb) { return has_peace_option(cb, kPoAnnex) || has_peace_option(cb, kPoTransferProvinces); }

std::string check_term(uintptr_t action, const Term& term) {
    if (cb_unsupported(term.cb)) {
        return text("SE_TREATY_TERM_ANNEX");
    }
    if (!cb_usable(term.cb, term.actor, term.receiver)) {
        return text("SE_TREATY_TERM_INVALID");
    }
    uint8_t g[kWarGoalSize];
    build_goal(g, term);
    return term_problem(action, reinterpret_cast<uintptr_t>(g));
}

void send(uintptr_t action, uintptr_t gui_owner) {
    using PtrFn = uintptr_t(__fastcall*)(uintptr_t, void*);
    uintptr_t command = read<PtrFn>(read<uintptr_t>(action) + 0x18)(action, nullptr);  // CreateCommand
    uintptr_t owner = read<PtrFn>(read<uintptr_t>(gui_owner) + kOwnerSessionOwner)(gui_owner, nullptr);
    uintptr_t session = read<uintptr_t>(owner + kSessionOwnerSession);
    using QueueFn = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    read<QueueFn>(read<uintptr_t>(session) + kSessionQueueCommand)(session, nullptr, command);
    logf("treaty: %d proposes to %d (%u terms, money %lld)", read<int>(action + kActorIndex),
         read<int>(action + kRecipientIndex), static_cast<unsigned>(goal_count(action)),
         static_cast<long long>(money(action) / kMoneyOne));
}

}  // namespace treaty

void install_treaty(const Config& config) {
    if (!config.treaty) {
        return;
    }
    g_config = config;
    build_vtable();
    g_ai_back = hooks::exe_va(kAiTypeSwitch) + sizeof(kAiTypeSwitchBytes);
    g_ai_done = hooks::exe_va(kAiChanceDone);

    // The action side first: inert until the token exists.
    uintptr_t create = hooks::exe_va(kCreateByType);
    if (!hooks::expect_bytes(create, kCreateByTypeEntry, sizeof(kCreateByTypeEntry)) ||
        !(g_create_trampoline = hooks::make_trampoline(create, sizeof(kCreateByTypeEntry))) ||
        !hooks::jump_hook(create, kCreateByTypeEntry, sizeof(kCreateByTypeEntry),
                          reinterpret_cast<uintptr_t>(&create_by_type))) {
        logf("treaty: DiplomaticAction_CreateByType at %08X is not the expected one; off", kCreateByType);
        return;
    }
    if (!hooks::jump_hook(hooks::exe_va(kAiTypeSwitch), kAiTypeSwitchBytes, sizeof(kAiTypeSwitchBytes),
                          reinterpret_cast<uintptr_t>(&ai_thunk))) {
        logf("treaty: the AI's proposal switch at %08X is not the expected one; the AI declines treaties",
             kAiTypeSwitch);
    }
    if (!treaty_ui::install(config)) {
        logf("treaty: the screens could not be installed; off");
        return;
    }
    uintptr_t slot = hooks::exe_va(kCbOnKeywordSlot);
    g_prev_on_keyword = read<uintptr_t>(slot);
    if (!hooks::replace_pointer(slot, g_prev_on_keyword, reinterpret_cast<uintptr_t>(&on_keyword))) {
        logf("treaty: CCasusBelliType's OnKeyword slot could not be replaced; off");
        return;
    }
    uintptr_t site = hooks::exe_va(kCbManagerLoadCall);
    if (read<uint8_t>(site) != 0xE8) {
        logf("treaty: the CBManager_Load call at %08X is not a call; off", kCbManagerLoadCall);
        return;
    }
    g_prev_load = call_target(site);
    if (!hooks::redirect_call(site, g_prev_load, reinterpret_cast<uintptr_t>(&load_thunk))) {
        logf("treaty: the CBManager_Load call could not be redirected; off");
        return;
    }
    logf("treaty: on (is_treaty_cb in cb_types.txt)");
}

}  // namespace se::features
