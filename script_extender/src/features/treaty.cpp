#include "features/treaty.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "features/treaty_ai_dismantle.h"
#include "features/treaty_ui.h"
#include "game/console.h"
#include "game/effects.h"
#include "game/localisation.h"
#include "game/map.h"
#include "game/objects.h"
#include "game/script.h"
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
constexpr uint32_t kCbManagerLoad = 0x0090CDC0;      // stdcall (manager, block): the call's vanilla target
constexpr uint32_t kCbManagerLoadCall = 0x0063DCD2;
constexpr uint32_t kCbOnKeywordSlot = 0x00E33734;  // CCasusBelliType_vftable slot 4
constexpr uint32_t kCbOnKeyword = 0x0090A1E0;      // its vanilla method: thiscall (cb; parser, token), RET 8
constexpr uint32_t kCbHasPeaceOption = 0x0090A800;  // EAX = cb, ECX = token -> AL
constexpr int kTokenYes = 0x29F;
constexpr int kPoAnnex = 0x8A1;
constexpr uint32_t kParserValueToken = 0x228;  // token of the value just read
constexpr uint32_t kParserValueText = 0x22C;   // its text
constexpr uint32_t kCbName = 0x14;             // std::string
constexpr uint32_t kCbCanUse = 0x20;           // vt slot: bool (CEventScope*)
constexpr int kPoDemandState = 0x8A3;
constexpr int kPoTransferProvinces = 0x8A7;
constexpr int kPoColony = 0xA4C;
constexpr uint32_t kCbAllowedStatesChildren = 0xA8;     // allowed_states (CAndTrigger +0xA0): children vector
constexpr uint32_t kCbAllowedCountriesChildren = 0xE0;  // allowed_countries (+0xD8): children vector
constexpr uint32_t kCbAllAllowedStates = 0x1DC;         // u8
constexpr uint32_t kWarGoalTargetStates = 0x0094E4D0;   // ECX = out vector<CState*>, stdcall (goal)
constexpr uint32_t kEventScopeCtor = 0x008A8650;  // thiscall (parent)
constexpr uint32_t kEventScopeSize = 0x48;
constexpr uint32_t kScopeCountryTag = 0x08;
constexpr uint32_t kScopeCountryIndex = 0x0C;
constexpr uint32_t kScopeThis = 0x24;
constexpr uint32_t kScopeProvince = 0x18;   // province scope (as WarGoal_IsValid / GetTargetStates set it)
constexpr uint32_t kCbAllowedStates = 0xA0;  // CAndTrigger
constexpr uint32_t kStateProvinceIds = 0x48;  // vector<int>
constexpr uint32_t kStateOwnerIndex = 0x5C;
constexpr uint32_t kScopeFromTag = 0x2C;    // in the THIS scope, as WarGoal_IsValid writes the goal's country
constexpr uint32_t kScopeFromIndex = 0x30;
constexpr uint32_t kCbAllowedCountries = 0xD8;  // CAndTrigger
constexpr uint32_t kTriggerEvaluate = 0x18;     // trigger vt: bool (CEventScope*)

// Diplomatic actions
constexpr uint32_t kCreateByType = 0x0090E850;  // cdecl (int token) -> action
constexpr uint32_t kCanPerform = 0x00911760;
constexpr uint32_t kShowIncoming = 0x00913020;  // DiplomaticAction_ShowIncoming: thiscall (owner), DefaultDialog
constexpr uint32_t kCheatAiAcceptAll = 0x012589A7;  // u8, toggled by the yesmen console command
constexpr uint32_t kUnitsSumStrength = 0x005DCBD0;  // stdcall (unit list, int* land, int* naval), adds    // DiplomaticAction_CanPerform: ESI = action, (bool) -> AL
constexpr uint8_t kCreateByTypeEntry[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr uint32_t kPeaceVtable = 0x00E34474;
constexpr uint32_t kBaseActionVtable = 0x00E3405C;
constexpr uint32_t kPeaceCtorDefault = 0x00916A30;  // stdcall (this, date, actorTag, actorIdx, recTag, recIdx)
constexpr uint32_t kPeaceCopyCtor = 0x00916B50;     // stdcall (this, src)
constexpr uint32_t kPeaceWriteFields = 0x00919660;  // thiscall (stream)
constexpr uint32_t kPeaceOnKeyword = 0x00919690;    // thiscall (parser, token)
constexpr uint32_t kNullDate = 0x01259ABC;
constexpr uint32_t kActionSize = 0xA8;
constexpr uint32_t kSlotCount = 38;
// CDiplomaticAction / CPeaceAction fields
constexpr uint32_t kActorTag = 0x08;
constexpr uint32_t kActorIndex = 0x0C;
constexpr uint32_t kRecipientIndex = 0x14;
constexpr uint32_t kFlags = 0x44;   // offer+0x1C: vector<int>, parallel to the goals
constexpr uint32_t kGoals = 0x64;   // offer+0x3C: vector<CWarGoal> (0x40 each)
constexpr uint32_t kMoney = 0x98;   // ours: i64, actor -> recipient
constexpr uint32_t kBudget = 0xA0;  // ours: points of a forced (dismantle) treaty; 0 = an ordinary one
constexpr int kNoticeBudget = -1;   // ... or: the terms an AI imposed on a human, shown to it (never sent)
// The AI's pending proposals: the type switch
constexpr uint32_t kAiTypeSwitch = 0x00820EBB;  // MOV EAX,[ESI]; MOV EDX,[EAX+0x20]; MOV ECX,ESI
constexpr uint8_t kAiTypeSwitchBytes[] = {0x8B, 0x06, 0x8B, 0x50, 0x20, 0x8B, 0xCE};
constexpr uint32_t kAiChanceDone = 0x0082102B;  // CMP EAX, 0x32: the chance decides

// War goals
constexpr uint32_t kWarGoalSize = 0x40;
constexpr uint32_t kWarGoalCtor = 0x0094BDB0;     // EAX = this, ECX = cb, (actorTag, actorIdx, recTag, recIdx)
constexpr uint32_t kWarGoalExecute = 0x0094E660;  // stdcall (goal)
constexpr uint32_t kVassalRelease = 0x0094FF30;   // (CountryRef* vassal, CountryRef* overlord)
constexpr uint32_t kWarGoalPeaceCost = 0x00951920;  // stdcall (goal, int* out, bool)
constexpr uint32_t kWarGoalInfamy = 0x009500E0;     // EDI = int* out, stdcall (goal)
constexpr uint32_t kWarGoalIsValid = 0x00952850;    // thiscall ECX = goal, (war, crisis, 0) -> AL
constexpr uint32_t kGoalCb = 0x0C;
constexpr uint32_t kGoalThirdTag = 0x10;
constexpr uint32_t kGoalThirdIndex = 0x14;
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
constexpr uint32_t kCountryNeighbours = 0xD08;  // u8 per country index (the neighbour trigger)
constexpr uint32_t kCountryIsSubstate = 0xCF4;
constexpr uint32_t kCountryIsVassal = 0xCF5;
constexpr uint32_t kCountryOverlord = 0xCFC;      // index, with the tag at +0xCF8
constexpr uint32_t kCountrySphereLeader = 0x142C;  // index, with the tag at +0x1428
constexpr uint32_t kCountryStateCount = 0xE4C;
constexpr uint32_t kCountryCapital = 0xBE4;
constexpr uint32_t kCountryTreasury = 0xE78;   // i64, 2^15 fixed point
constexpr uint32_t kCountryInfamy = 0x1430;    // x1000
constexpr uint32_t kRelationWar = 0x34;
constexpr uint32_t kRelationAlliance = 0x28;  // agreement object (War_CanDeclare)
constexpr uint32_t kRelationValue = 0x3C;
constexpr uint32_t kRelationMax = 0x0131B1E8;
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kProvinceController = 0x134;
constexpr uint32_t kProvinceState = 0x188;
constexpr uint32_t kProvinceCores = 0x148;  // vector<CountryRef> {tag, index}
constexpr uint32_t kProvinceAddCore = 0x004B61A0;  // stdcall (province, CountryRef*, bool history), as add_core
constexpr uint32_t kCountryCoresChanged = 0x1434;  // u8, set by add_core after Province_AddCore
constexpr uint32_t kWorldHumans = 0xAEC;    // vector<int>: 0 = AI
constexpr uint32_t kWorldMilitary = 0xAB4;
constexpr uint32_t kWorldRunning = 0xD1C;
constexpr uint32_t kValidateCapital = 0x0050E540;      // stdcall (country)
constexpr uint32_t kRepositionArmies = 0x005B53D0;     // stdcall (world+0xAB4, country)
constexpr uint32_t kUpdatePortBlockade = 0x004B7C50;   // EDI = province
constexpr uint32_t kUpdateHostileFlags = 0x005DBD90;   // EAX = unit list
constexpr uint32_t kCalculateDistribution = 0x00520150;  // stdcall (country)
constexpr uint32_t kAiDailyUpdate = 0x0085CCB0;        // EAX = country + 0x364

// Peace and dismantling
constexpr uint32_t kPeaceExecuteSlot = 0x00E344F4;  // CPeaceAction_vftable slot 32
constexpr uint32_t kPeaceExecute = 0x00916F00;      // thiscall (ctx), RET 4
constexpr uint32_t kWarAttackers = 0x2C;  // vector<{tag, index}>, the leader first
constexpr uint32_t kWarDefenders = 0x3C;
constexpr uint32_t kAddPendingAction = 0x005164C0;  // Country_AddPendingAction: ESI = country, EDI = action
constexpr uint32_t kCountryPending = 0xCE4;         // list {action, prev, next}; +4 tail, +8 count
constexpr uint32_t kCountryGreatPower = 0x31;       // u8
constexpr uint32_t kCountryCivilized = 0x12D0;      // u8
constexpr uint32_t kCountryRank = 0x1404;
constexpr uint32_t kEconomyDefines = 0x012586DC;  // +0x40 country defines; there +0x50: secondary power rank x1000
constexpr uint32_t kWorldLocalPlayer = 0xB60;

// Script effects (docs/re/systems/effects.md)
constexpr int kTokenReleaseVassal = 0x700;
constexpr uint32_t kReleaseVassalEffectVtable = 0x00E2B4AC;  // CReleaseVassalEffect, 0x2C bytes
using game::kEffectSlots;  // the factory hook and the slots: game/effects.h
constexpr uint32_t kEffectUsesThis = 0x1C;     // u8: `= THIS`
constexpr uint32_t kEffectUsesFrom = 0x1D;     // u8: `= FROM`
constexpr uint32_t kEffectTargetIndex = 0x24;  // `= TAG`: {tag, index} at +0x20
constexpr uint32_t kScopeThisProvince = 0x10;  // a province scope's province
constexpr uint32_t kScopeFromProvince = 0x34;  // FROM as a province

// Game memory (game::game_new / game_delete) and strings
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
constexpr const char* kTransferPuppetKeyword = "po_transfer_puppet";
constexpr const char* kBudgetKeyword = "se_treaty_budget";
constexpr const char* kFreeKeyword = "dismantle_free";  // a treaty CB: free and already chosen in a forced treaty
constexpr const char* kInfamyKeyword = "dismantle_infamy";  // a treaty CB: costs its infamy in a forced treaty too
constexpr const char* kCostKeyword = "dismantle_cost";      // a treaty CB: factor of its points in a forced treaty
constexpr const char* kRepeatKeyword = "repeat_cost";        // a treaty CB: factor for its further terms with one country
constexpr const char* kDismantlingKeyword = "is_dismantling_cb";  // a treaty CB: may be in a forced treaty
constexpr const char* kToThirdKeyword = "goes_to_third";  // a treaty CB: what the term takes goes to its third country
constexpr const char* kPartitionKeyword = "po_partition";  // a treaty CB: its countries and states are common/se_partitions.txt's
constexpr const char* kAiWeightKeyword = "ai_dismantle_weight";  // a treaty CB: factor of its value to an AI dismantler
constexpr const wchar_t* kPartitionsFile = L"common\\se_partitions.txt";
constexpr const wchar_t* kInterestsFile = L"common\\ai_interests.txt";
constexpr int64_t kMoneyOne = 32768;  // 1.0 in the treasury's fixed point

Config g_config;
int g_token = 0;         // is_treaty_cb
int g_action_token = 0;  // treaty_action
int g_money_token = 0;   // se_treaty_money
int g_transfer_puppet_token = 0;  // po_transfer_puppet
// The dismantle effects: `dismantle = TAG` (the scope dismantles TAG), `dismantled_by = TAG` (TAG dismantles
// the scope; in a CB's on_po_accepted the scope is the loser and THIS the goal's holder), each with a _gw
// form (treaty_dismantle_gw% of the points).
struct DismantleEffect {
    const char* keyword;
    bool gw;
    bool reversed;  // dismantled_by: the target dismantles the scope
    int token;
};
DismantleEffect g_dismantle_effects[] = {
    {"dismantle", false, false, 0},
    {"dismantle_gw", true, false, 0},
    {"dismantled_by", false, true, 0},
    {"dismantled_by_gw", true, true, 0},
};
constexpr size_t kDismantleEffects = sizeof(g_dismantle_effects) / sizeof(g_dismantle_effects[0]);
int g_budget_token = 0;           // se_treaty_budget
int g_free_token = 0;             // dismantle_free
int g_infamy_token = 0;           // dismantle_infamy
int g_cost_token = 0;             // dismantle_cost
int g_repeat_token = 0;           // repeat_cost
int g_dismantling_token = 0;       // is_dismantling_cb
int g_to_third_token = 0;          // goes_to_third
int g_partition_token = 0;         // po_partition
int g_ai_weight_token = 0;         // ai_dismantle_weight
// The CB lists, filled while cb_types.txt loads. Function-local statics: TGL_PluginInit runs under the
// loader lock, where a file-scope std::vector / std::map constructor would run (docs/CODING.md, 1); these
// are first built from the on_keyword hook, on the game thread.
using CbList = std::vector<uintptr_t>;
using CbPercent = std::map<uintptr_t, int>;
CbList& g_cbs() { static CbList v; return v; }                   // is_treaty_cb CBs, in cb_types.txt order
CbList& g_free_cbs() { static CbList v; return v; }              // dismantle_free CBs
CbList& g_infamy_cbs() { static CbList v; return v; }            // dismantle_infamy CBs
CbList& g_dismantling_cbs() { static CbList v; return v; }       // is_dismantling_cb CBs
CbList& g_to_third_cbs() { static CbList v; return v; }          // goes_to_third CBs
CbList& g_partition_cbs() { static CbList v; return v; }         // po_partition CBs
CbList& g_transfer_puppet_cbs() { static CbList v; return v; }   // the giver's chosen puppet becomes the gainer's
CbPercent& g_cost_percent() { static CbPercent v; return v; }    // dismantle_cost of a CB, in percent
CbPercent& g_repeat_percent() { static CbPercent v; return v; }  // repeat_cost of a CB, in percent
CbPercent& g_ai_weight_percent() { static CbPercent v; return v; }  // ai_dismantle_weight of a CB, in percent
uintptr_t g_prev_load = 0;       // what the CBManager_Load call called before us
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

using game::game_delete;
using game::game_new;

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
    for (uintptr_t c : g_cbs()) {
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
    "SE_TREATY_MONEY_SHORT",   "SE_TREATY_TERM_SIDES",    "SE_TREATY_TERM_DUPLICATE",
};

// The texts of kTextKeys, read once on first use (from a hook, never under the loader lock).
const std::map<std::string, std::string>& texts() {
    static const std::map<std::string, std::string> table =
        game::read_localisation({std::begin(kTextKeys), std::end(kTextKeys)});
    return table;
}

// The English text of `key` (mod/script_extender/localisation), or the key itself when it is missing or
// empty. For display and the log only: nothing on the lockstep path compares texts (the problem functions
// below return keys), so a localisation file cannot change what a treaty does.
std::string text(const char* key) {
    auto it = texts().find(key);
    return it == texts().end() || it->second.empty() ? std::string(key) : it->second;
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
    if (count && !storage) {
        return;  // out of game memory: the vector keeps what it had
    }
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

bool transfers_puppet(uintptr_t cb) {
    return std::find(g_transfer_puppet_cbs().begin(), g_transfer_puppet_cbs().end(), cb) != g_transfer_puppet_cbs().end();
}

// A treaty CB with `goes_to_third = yes`: what the term takes goes to its third country, not to the gainer.
bool goes_to_third(uintptr_t cb) {
    return std::find(g_to_third_cbs().begin(), g_to_third_cbs().end(), cb) != g_to_third_cbs().end();
}

// --- Partitions ----------------------------------------------------------------------------------
// common/se_partitions.txt (the -mod= folders', else the plugin's): which country a loser can be broken
// into, whoever holds the land now. `TAG = { REGION ... provinces = { id ... } }`, regions of map/region.txt.
// A po_partition CB lists the tags with land of theirs held by the giver, then the giver's states holding
// some; the term is a po_transfer_provinces one (the whole state goes) and its listed provinces become
// the tag's cores.

// A treaty CB with `po_partition = yes`.
bool is_partition_cb(uintptr_t cb) {
    return std::find(g_partition_cbs().begin(), g_partition_cbs().end(), cb) != g_partition_cbs().end();
}

using game::read_file;
using game::script_words;

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

bool is_number(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// The tag's three letters as a country's +0x1C holds them.
uint32_t pack_tag(const std::string& tag) {
    uint32_t packed = 0;
    std::memcpy(&packed, tag.c_str(), 3);
    return packed;
}

std::once_flag g_partitions_once;
using Partitions = std::map<uint32_t, std::vector<uint32_t>>;
Partitions& g_partitions() { static Partitions v; return v; }  // tag -> its provinces, sorted (load_partitions)

// A file of `TAG = { REGION ... provinces = { id ... } }` blocks (the mod's, else the plugin's) into
// tag -> sorted province ids. Regions are those of map/region.txt.
void load_tag_lists(const wchar_t* file, Partitions& out, const char* what) {
    std::wstring path = game::game_file(file);
    if (path.empty()) {
        path = std::wstring(host().plugin_dir) + L"\\" + file;
    }
    std::vector<std::string> words = script_words(read_file(path));
    if (words.empty()) {
        logf("treaty: no %ls; no %s", file, what);
        return;
    }
    // map/region.txt: `REGION = { id ... }`
    std::map<std::string, std::vector<uint32_t>> regions;
    std::vector<std::string> region_words = script_words(read_file(game::game_file(L"map\\region.txt")));
    for (size_t i = 0; i + 2 < region_words.size(); ++i) {
        if (region_words[i + 1] != "=" || region_words[i + 2] != "{") {
            continue;
        }
        std::vector<uint32_t>& ids = regions[region_words[i]];
        for (i += 3; i < region_words.size() && region_words[i] != "}"; ++i) {
            if (is_number(region_words[i])) {
                ids.push_back(static_cast<uint32_t>(std::strtoul(region_words[i].c_str(), nullptr, 10)));
            }
        }
    }
    // `TAG = { REGION ... provinces = { id ... } }`
    for (size_t i = 0; i + 2 < words.size(); ++i) {
        if (words[i].size() != 3 || words[i + 1] != "=" || words[i + 2] != "{") {
            logf("treaty: %ls: unexpected '%s'", file, words[i].c_str());
            continue;
        }
        std::vector<uint32_t>& ids = out[pack_tag(words[i])];
        int depth = 1;
        bool listing = false;  // inside provinces = { }
        for (i += 3; i < words.size() && depth > 0; ++i) {
            const std::string& w = words[i];
            if (w == "{") {
                ++depth;
            } else if (w == "}") {
                --depth;
                listing = false;
            } else if (w == "provinces") {
                listing = true;
            } else if (is_number(w)) {
                ids.push_back(static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, 10)));
            } else if (w != "=" && !listing) {
                auto it = regions.find(w);
                if (it == regions.end()) {
                    logf("treaty: %ls: no region %s", file, w.c_str());
                } else {
                    ids.insert(ids.end(), it->second.begin(), it->second.end());
                }
            }
        }
        --i;  // the loop's ++i
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    logf("treaty: %u %s from %ls", static_cast<unsigned>(out.size()), what, path.c_str());
}

void load_partitions() { load_tag_lists(kPartitionsFile, g_partitions(), "partitions"); }

// common/ai_interests.txt: the land a country wants (same format). An AI dismantler takes it, or gives an
// ally its, before colonies and other land (treaty_dismantle_ai_interest).
std::once_flag g_interests_once;
Partitions& g_interests() { static Partitions v; return v; }

const std::vector<uint32_t>* interests_of(int country) {
    std::call_once(g_interests_once, [] { load_tag_lists(kInterestsFile, g_interests(), "interest lists"); });
    uintptr_t c = country > 0 ? game::country(country) : 0;
    if (!c) {
        return nullptr;
    }
    auto it = g_interests().find(read<uint32_t>(c + kCountryTag) & 0xFFFFFF);
    return it == g_interests().end() || it->second.empty() ? nullptr : &it->second;
}

bool province_of_interest(uint32_t province, int country) {
    const std::vector<uint32_t>* ids = interests_of(country);
    return ids && std::binary_search(ids->begin(), ids->end(), province);
}

// One of `states` holds a province of `country`'s interest.
bool of_interest(const std::vector<uintptr_t>& states, int country) {
    const std::vector<uint32_t>* ids = interests_of(country);
    if (!ids) {
        return false;
    }
    for (uintptr_t state : states) {
        uintptr_t end = read<uintptr_t>(state + kStateProvinceIds + 4);
        for (uintptr_t p = read<uintptr_t>(state + kStateProvinceIds); p < end; p += 4) {
            if (std::binary_search(ids->begin(), ids->end(), read<uint32_t>(p))) {
                return true;
            }
        }
    }
    return false;
}

// The provinces of the country's partition, or null when it has none.
const std::vector<uint32_t>* partition_of(int country) {
    std::call_once(g_partitions_once, load_partitions);
    uintptr_t c = game::country(country);
    if (!c) {
        return nullptr;
    }
    auto it = g_partitions().find(read<uint32_t>(c + kCountryTag) & 0xFFFFFF);
    return it == g_partitions().end() || it->second.empty() ? nullptr : &it->second;
}

bool in_partition(const std::vector<uint32_t>& ids, uint32_t province) {
    return std::binary_search(ids.begin(), ids.end(), province);
}

// The giver holds land of `third`'s partition, and `third` is no great power (the table holds every core
// of the mod, homelands of the great powers too: land for them is a cession, not a partition).
bool partition_allows(int gains, int gives, int third) {
    const std::vector<uint32_t>* ids = partition_of(third);
    if (!ids || third == gains || third == gives || read<uint8_t>(game::country(third) + kCountryGreatPower)) {
        return false;
    }
    uintptr_t giver = game::country(gives);
    for (const int* p = read<const int*>(giver + kCountryProvinces); p < read<const int*>(giver + kCountryProvinces + 4); ++p) {
        if (in_partition(*ids, static_cast<uint32_t>(*p))) {
            return true;
        }
    }
    return false;
}

// The state holding `province` has a province of `third`'s partition.
bool state_in_partition(uint32_t province, int third) {
    const std::vector<uint32_t>* ids = partition_of(third);
    uintptr_t state = province ? read<uintptr_t>(game::province(province) + kProvinceState) : 0;
    if (!ids || !state) {
        return false;
    }
    for (const uint32_t* p = read<const uint32_t*>(state + kStateProvinceIds); p < read<const uint32_t*>(state + kStateProvinceIds + 4); ++p) {
        if (in_partition(*ids, *p)) {
            return true;
        }
    }
    return false;
}

// Before a partition term runs: the provinces of the third country's partition in the state it takes
// become its cores (Province_AddCore, as add_core), so the country is released or grows with them.
void add_partition_cores(uintptr_t g) {
    int third = read<int>(g + kGoalThirdIndex);
    const std::vector<uint32_t>* ids = partition_of(third);
    uintptr_t state = read<uintptr_t>(game::province(read<uint32_t>(g + kGoalProvince)) + kProvinceState);
    if (!ids || !state) {
        return;
    }
    uint32_t ref[2] = {read<uint32_t>(game::country(third) + kCountryTag), static_cast<uint32_t>(third)};
    uint32_t* ref_ptr = ref;
    uintptr_t add_core = hooks::exe_va(kProvinceAddCore);
    std::vector<uint32_t> ids_in_state;
    for (const uint32_t* p = read<const uint32_t*>(state + kStateProvinceIds); p < read<const uint32_t*>(state + kStateProvinceIds + 4); ++p) {
        if (in_partition(*ids, *p)) {
            ids_in_state.push_back(*p);
        }
    }
    for (uint32_t id : ids_in_state) {
        uintptr_t province = game::province(id);
        __asm {
            mov esi, esp
            push 1
            push ref_ptr
            push province
            call add_core
            mov esp, esi
        }
    }
    *reinterpret_cast<uint8_t*>(game::country(third) + kCountryCoresChanged) = 1;
}

void build_goal(void* out, const treaty::Term& term);

// The goal a term with a third country amounts to; it is priced, infamy counted and executed as that one.
// po_transfer_puppet: the po_make_puppet goal of the gainer over the third country. goes_to_third: the same
// CB and place, with the third country (an ally) as the one who gains. Else the term itself.
uintptr_t puppet_goal(uintptr_t g, uint8_t (&buffer)[kWarGoalSize]) {
    uintptr_t cb = read<uintptr_t>(g + kGoalCb);
    int third = read<int>(g + kGoalThirdIndex);
    if (!third) {
        return g;
    }
    if (transfers_puppet(cb)) {
        build_goal(buffer, {cb, read<int>(g + kGoalActorIndex), third, 0, 0});
        return reinterpret_cast<uintptr_t>(buffer);
    }
    if (goes_to_third(cb)) {
        build_goal(buffer, {cb, third, read<int>(g + kGoalReceiverIndex), read<uint32_t>(g + kGoalProvince), 0});
        return reinterpret_cast<uintptr_t>(buffer);
    }
    return g;
}

int peace_cost(uintptr_t goal_ptr) {
    uint8_t buffer[kWarGoalSize];
    goal_ptr = puppet_goal(goal_ptr, buffer);
    int out = 0;
    using CostFn = int*(__stdcall*)(uintptr_t, int*, bool);
    fn<CostFn>(kWarGoalPeaceCost)(goal_ptr, &out, false);
    return out;
}

int infamy(uintptr_t goal_ptr) {
    uint8_t buffer[kWarGoalSize];
    goal_ptr = puppet_goal(goal_ptr, buffer);
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
    if (term.third) {
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(out) + kGoalThirdTag) =
            read<uint32_t>(game::country(term.third) + kCountryTag);
        *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(out) + kGoalThirdIndex) = term.third;
    }
}

// The states a goal takes (WarGoal_GetTargetStates), for the moving options' last-state guard.
std::vector<uintptr_t> target_states(uintptr_t goal_ptr) {
    uintptr_t vec[3] = {0, 0, 0};
    uintptr_t* states_out = vec;
    uintptr_t f = hooks::exe_va(kWarGoalTargetStates);
    __asm {
        mov ecx, states_out
        push goal_ptr
        call f
    }
    std::vector<uintptr_t> states;
    for (uintptr_t p = vec[0]; p < vec[1]; p += 4) {
        states.push_back(read<uintptr_t>(p));
    }
    if (vec[0]) {
        game_delete(reinterpret_cast<void*>(vec[0]));
    }
    return states;
}

bool scoped_allowed_state(uintptr_t cb, int gains, int third, uint32_t province) {
    // As WarGoal_GetTargetStates: a province scope (+0x18) for the state's first province, THIS = the
    // gaining country, FROM = the third country in the evaluated scope.
    alignas(8) uint8_t scope[kEventScopeSize];
    alignas(8) uint8_t this_scope[kEventScopeSize];
    using ScopeCtorFn = void(__fastcall*)(void*, void*, void*);
    fn<ScopeCtorFn>(kEventScopeCtor)(scope, nullptr, nullptr);
    fn<ScopeCtorFn>(kEventScopeCtor)(this_scope, nullptr, nullptr);
    *reinterpret_cast<uint32_t*>(this_scope + kScopeCountryTag) = read<uint32_t>(game::country(gains) + kCountryTag);
    *reinterpret_cast<int*>(this_scope + kScopeCountryIndex) = gains;
    *reinterpret_cast<void**>(scope + kScopeThis) = this_scope;
    *reinterpret_cast<uint32_t*>(scope + kScopeProvince) = province;
    for (uint8_t* sc : {scope, this_scope}) {
        *reinterpret_cast<uint32_t*>(sc + kScopeFromTag) = read<uint32_t>(game::country(third) + kCountryTag);
        *reinterpret_cast<int*>(sc + kScopeFromIndex) = third;
    }
    uintptr_t trigger = cb + kCbAllowedStates;
    using EvalFn = bool(__fastcall*)(uintptr_t, void*, void*);
    return read<EvalFn>(read<uintptr_t>(trigger) + kTriggerEvaluate)(trigger, nullptr, scope);
}

bool scoped_allowed_country(uintptr_t cb, int gains, int gives, int third) {
    if (is_partition_cb(cb) && !partition_allows(gains, gives, third)) {
        return false;  // a partition's country, with land held by the giver (then allowed_countries too)
    }
    alignas(8) uint8_t scope[kEventScopeSize];
    alignas(8) uint8_t this_scope[kEventScopeSize];
    using ScopeCtorFn = void(__fastcall*)(void*, void*, void*);
    fn<ScopeCtorFn>(kEventScopeCtor)(scope, nullptr, nullptr);
    fn<ScopeCtorFn>(kEventScopeCtor)(this_scope, nullptr, nullptr);
    auto set_country = [](uint8_t* s, int index) {
        *reinterpret_cast<uint32_t*>(s + kScopeCountryTag) = read<uint32_t>(game::country(index) + kCountryTag);
        *reinterpret_cast<int*>(s + kScopeCountryIndex) = index;
    };
    set_country(scope, gives);
    set_country(this_scope, gains);
    *reinterpret_cast<void**>(scope + kScopeThis) = this_scope;
    // The candidate as FROM: in the THIS scope, where conditions on the scope find it (as WarGoal_IsValid
    // puts it), and in the scope itself, which nested scopes copy (EventScope_Copy): any_owned_province =
    // { is_core = FROM } reads it there.
    for (uint8_t* s : {scope, this_scope}) {
        *reinterpret_cast<uint32_t*>(s + kScopeFromTag) = read<uint32_t>(game::country(third) + kCountryTag);
        *reinterpret_cast<int*>(s + kScopeFromIndex) = third;
    }
    uintptr_t trigger = cb + kCbAllowedCountries;
    using EvalFn = bool(__fastcall*)(uintptr_t, void*, void*);
    return read<EvalFn>(read<uintptr_t>(trigger) + kTriggerEvaluate)(trigger, nullptr, scope);
}

void log_conditions_impl(uintptr_t cb, int gains, int gives, int third) {
    alignas(8) uint8_t scope[kEventScopeSize];
    alignas(8) uint8_t this_scope[kEventScopeSize];
    using ScopeCtorFn = void(__fastcall*)(void*, void*, void*);
    fn<ScopeCtorFn>(kEventScopeCtor)(scope, nullptr, nullptr);
    fn<ScopeCtorFn>(kEventScopeCtor)(this_scope, nullptr, nullptr);
    auto set_country = [](uint8_t* s, int index) {
        *reinterpret_cast<uint32_t*>(s + kScopeCountryTag) = read<uint32_t>(game::country(index) + kCountryTag);
        *reinterpret_cast<int*>(s + kScopeCountryIndex) = index;
    };
    set_country(scope, gives);
    set_country(this_scope, gains);
    *reinterpret_cast<void**>(scope + kScopeThis) = this_scope;
    for (uint8_t* s : {scope, this_scope}) {
        *reinterpret_cast<uint32_t*>(s + kScopeFromTag) = read<uint32_t>(game::country(third) + kCountryTag);
        *reinterpret_cast<int*>(s + kScopeFromIndex) = third;
    }
    uintptr_t begin = read<uintptr_t>(cb + kCbAllowedCountriesChildren);
    uintptr_t end = read<uintptr_t>(cb + kCbAllowedCountriesChildren + 4);
    using EvalFn = bool(__fastcall*)(uintptr_t, void*, void*);
    for (uintptr_t p = begin; p < end; p += 4) {
        uintptr_t child = read<uintptr_t>(p);
        bool ok = read<EvalFn>(read<uintptr_t>(child) + kTriggerEvaluate)(child, nullptr, scope);
        uint32_t vtable = static_cast<uint32_t>(read<uintptr_t>(child) - hooks::exe_base() + hooks::kListingBase);
        logf("treaty:     condition %u (vtable %08X): %s", static_cast<unsigned>((p - begin) / 4), vtable, ok ? "yes" : "no");
    }
}


// The war goal's validity. Without a third country, WarGoal_IsValid. With one (allowed_countries), as the
// peace's execution scopes it: WarGoal_IsValid keeps FROM in the THIS scope only, so nested conditions
// (any_owned_province = { is_core = FROM }) never see it and it refuses every liberation; the game's own
// WarGoal_GetTargetStates puts FROM in the evaluated scope, and that is what the execution uses.
bool goal_valid_scoped(uintptr_t g) {
    int third = read<int>(g + kGoalThirdIndex);
    if (!third) {
        return goal_valid(g);
    }
    uintptr_t cb = read<uintptr_t>(g + kGoalCb);
    int gains = read<int>(g + kGoalActorIndex);
    int gives = read<int>(g + kGoalReceiverIndex);
    if (!exists(gives) || !scoped_allowed_country(cb, gains, gives, third)) {
        return false;
    }
    if (is_partition_cb(cb) && !state_in_partition(read<uint32_t>(g + kGoalProvince), third)) {
        return false;  // the state holds none of the partition's land
    }
    if (read<uintptr_t>(cb + kCbAllowedStatesChildren + 4) == read<uintptr_t>(cb + kCbAllowedStatesChildren)) {
        return true;
    }
    // all_allowed_states: the game's WarGoal_GetTargetStates (already filtered by allowed_states);
    // otherwise the state of the chosen province.
    std::vector<uintptr_t> states;
    if (read<uint8_t>(cb + kCbAllAllowedStates)) {
        states = target_states(g);
    } else if (uint32_t id = read<uint32_t>(g + kGoalProvince)) {
        states.push_back(read<uintptr_t>(game::province(id) + kProvinceState));
    }
    if (states.empty()) {
        logf("treaty:   goal check, third %d: no target states (all_allowed_states %d)", third,
             read<uint8_t>(cb + kCbAllAllowedStates));
        return false;
    }
    for (uintptr_t st : states) {
        uintptr_t ids = st + kStateProvinceIds;
        if (read<uintptr_t>(ids) == read<uintptr_t>(ids + 4) || read<int>(st + kStateOwnerIndex) != gives) {
            logf("treaty:   goal check, third %d: one of %u states has %d provinces, owner %d (giver %d)", third,
                 static_cast<unsigned>(states.size()),
                 static_cast<int>((read<uintptr_t>(ids + 4) - read<uintptr_t>(ids)) / 4),
                 read<int>(st + kStateOwnerIndex), gives);
            return false;
        }
        if (!read<uint8_t>(cb + kCbAllAllowedStates) &&
            !scoped_allowed_state(cb, gains, third, read<uint32_t>(read<uintptr_t>(ids)))) {
            return false;
        }
    }
    return true;
}

// The problem functions return the localisation key of the reason (one of kTextKeys), or nullptr when
// there is none. They run on the lockstep path (slot_execute, ai_chance, the AI dismantler), so the
// answer is never a text: text() localises a key for the editor and the log only.
using Problem = const char*;
constexpr Problem kNoProblem = nullptr;

// Null when the term at `g` can be applied between the treaty's two countries now, else why not.
Problem term_problem(uintptr_t action, uintptr_t g) {
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    uintptr_t cb = read<uintptr_t>(g + kGoalCb);
    int gains = read<int>(g + kGoalActorIndex);
    int gives = read<int>(g + kGoalReceiverIndex);
    if (!cb || !is_treaty_cb(cb)) {
        return "SE_TREATY_TERM_INVALID";
    }
    if (!goal_valid_scoped(g)) {
        return "SE_TREATY_TERM_INVALID";
    }
    if (!((gains == a && gives == r) || (gains == r && gives == a))) {
        return "SE_TREATY_TERM_SIDES";
    }
    if (has_peace_option(cb, kPoAnnex) || has_peace_option(cb, kPoColony)) {
        return "SE_TREATY_TERM_ANNEX";
    }
    if (has_peace_option(cb, kPoDemandState) || has_peace_option(cb, kPoTransferProvinces)) {
        // Provinces move: the giver keeps at least one state, and its capital. No capital (the country is
        // being released, or an earlier term of the same treaty emptied it): nothing can move.
        uintptr_t giver = game::country(gives);
        uint32_t capital = read<uint32_t>(giver + kCountryCapital);
        if (!capital) {
            return "SE_TREATY_TERM_LAST_STATE";
        }
        uintptr_t capital_state = read<uintptr_t>(game::province(capital) + kProvinceState);
        std::vector<uintptr_t> states;
        if (read<uint8_t>(cb + kCbAllAllowedStates)) {
            states = target_states(g);
        } else if (uint32_t id = read<uint32_t>(g + kGoalProvince)) {
            states.push_back(read<uintptr_t>(game::province(id) + kProvinceState));
        }
        for (uintptr_t st : states) {
            if (st == capital_state) {
                return "SE_TREATY_TERM_LAST_STATE";
            }
        }
        if (static_cast<int>(states.size()) >= read<int>(giver + kCountryStateCount)) {
            return "SE_TREATY_TERM_LAST_STATE";
        }
    }
    uint32_t id = read<uint32_t>(g + kGoalProvince);
    if (id) {
        uintptr_t province = game::province(id);
        if (read<int>(province + kProvinceOwner) != gives) {
            return "SE_TREATY_TERM_OWNER";
        }
        if (read<int>(province + kProvinceController) != gives) {
            return "SE_TREATY_TERM_OCCUPIED";
        }
        if (read<int>(game::country(gives) + kCountryStateCount) <= 1) {
            return "SE_TREATY_TERM_LAST_STATE";
        }
    }
    return kNoProblem;
}

// --- Forced (dismantle) treaties -------------------------------------------------------------------

int budget_of(uintptr_t action) { return read<int>(action + kBudget); }

bool is_forced(uintptr_t action) { return budget_of(action) > 0; }

bool is_notice(uintptr_t action) { return budget_of(action) == kNoticeBudget; }

// A treaty CB with `dismantle_free = yes`: free in a forced treaty, and chosen in it from the start.
// A treaty CB with `is_dismantling_cb = yes`: only these may be terms of a forced (dismantle) treaty.
bool is_dismantling_cb(uintptr_t cb) {
    return std::find(g_dismantling_cbs().begin(), g_dismantling_cbs().end(), cb) != g_dismantling_cbs().end();
}

bool is_free_cb(uintptr_t cb) { return std::find(g_free_cbs().begin(), g_free_cbs().end(), cb) != g_free_cbs().end(); }

// A treaty CB with `dismantle_infamy = yes`: in a forced treaty it costs its infamy, as in any treaty.
bool charges_infamy(uintptr_t cb) { return std::find(g_infamy_cbs().begin(), g_infamy_cbs().end(), cb) != g_infamy_cbs().end(); }

// Percent of a term's war score that it costs in a forced treaty: 0 when free, else `dismantle_cost`.
int dismantle_percent(uintptr_t cb) {
    if (is_free_cb(cb)) {
        return 0;
    }
    auto it = g_cost_percent().find(cb);
    return it == g_cost_percent().end() ? 100 : it->second;
}

// The points of the term at `g` in a forced treaty.
int dismantle_points(uintptr_t g) {
    return static_cast<int>(static_cast<int64_t>(peace_cost(g)) * dismantle_percent(read<uintptr_t>(g + kGoalCb)) / 100000);
}

// The infamy (x1000) the dismantle_infamy terms of a forced treaty cost.
int forced_term_infamy(uintptr_t action) {
    int total = 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        if (selected(action, i) && charges_infamy(read<uintptr_t>(goal(action, i) + kGoalCb))) {
            total += infamy(goal(action, i));
        }
    }
    return total;
}

// Percent the term at `i` costs for repeating: `repeat_cost` when an earlier selected term has the same CB
// and the same third country (more states freed for one people), else 100.
int repeat_percent(uintptr_t action, size_t i) {
    uintptr_t g = goal(action, i);
    uintptr_t cb = read<uintptr_t>(g + kGoalCb);
    auto it = g_repeat_percent().find(cb);
    if (it == g_repeat_percent().end()) {
        return 100;
    }
    int third = read<int>(g + kGoalThirdIndex);
    for (size_t j = 0; j < i; ++j) {
        uintptr_t other = goal(action, j);
        if (selected(action, j) && read<uintptr_t>(other + kGoalCb) == cb && read<int>(other + kGoalThirdIndex) == third) {
            return it->second;
        }
    }
    return 100;
}

// The points of the term at `i` within the treaty: its war score (x dismantle_percent in a forced treaty),
// x repeat_percent.
int term_points_at(uintptr_t action, size_t i, bool forced) {
    uintptr_t g = goal(action, i);
    int64_t cost = peace_cost(g);  // x1000
    if (forced) {
        cost = cost * dismantle_percent(read<uintptr_t>(g + kGoalCb)) / 100;
    }
    return static_cast<int>(cost * repeat_percent(action, i) / 100 / 1000);
}

// What the winner takes in a forced treaty, in war-score points: each term's (term_points_at), and the
// money demanded.
int forced_points(uintptr_t action) {
    int points = 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        if (selected(action, i)) {
            points += term_points_at(action, i, true);
        }
    }
    int64_t demanded = -read<int64_t>(action + kMoney);
    int64_t per_point = g_config.treaty_ai_money_per_point > 0 ? g_config.treaty_ai_money_per_point : 1;
    if (demanded > 0) {
        points += static_cast<int>(demanded / (per_point * kMoneyOne));
    }
    return points;
}

// Infamy (x1000) for `over` points past the budget: the first costs min, each further one a little more,
// towards max (halfway there `half` points past).
int overflow_infamy(int over) {
    const Config& k = g_config;
    int64_t min = static_cast<int64_t>(k.treaty_dismantle_infamy_min) * 10;  // hundredths -> thousandths
    int64_t max = static_cast<int64_t>(k.treaty_dismantle_infamy_max) * 10;
    int64_t half = k.treaty_dismantle_infamy_half > 0 ? k.treaty_dismantle_infamy_half : 1;
    int64_t total = 0;
    for (int64_t i = 0; i < over; ++i) {
        total += min + (max - min) * i / (i + half);
    }
    return static_cast<int>(total);
}

// --- The treaty as a whole -----------------------------------------------------------------------

// Null when the treaty can be sent or applied as it is, else the key of the reason.
Problem treaty_problem(uintptr_t action) {
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    if (!exists(a) || !exists(r) || a == r) {
        return "SE_TREATY_GONE";
    }
    if (read<uintptr_t>(relation(a, r) + kRelationWar)) {
        return "SE_TREATY_AT_WAR";
    }
    bool forced = is_forced(action);
    int64_t m = read<int64_t>(action + kMoney);
    if (forced && m > 0) {
        return "SE_TREATY_TERM_SIDES";  // the loser only gives
    }
    size_t count = 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        if (!selected(action, i)) {
            continue;
        }
        ++count;
        uintptr_t g = goal(action, i);
        if (forced && (read<int>(g + kGoalActorIndex) != a || !is_dismantling_cb(read<uintptr_t>(g + kGoalCb)))) {
            return "SE_TREATY_TERM_SIDES";
        }
        if (Problem why = term_problem(action, g)) {
            return why;
        }
    }
    if (count == 0 && m == 0 && !forced) {  // a forced treaty may take nothing
        return "SE_TREATY_NO_TERMS";
    }
    int payer = m > 0 ? a : r;
    if (m != 0 && read<int64_t>(game::country(payer) + kCountryTreasury) < (m > 0 ? m : -m)) {
        return "SE_TREATY_MONEY_SHORT";
    }
    return kNoProblem;
}

// The AI's view of the treaty as `self`, in war-score points x 100 (>= 0 accepts). Reads synced state only.
//   gain     = terms it receives (x core_gain% when the province is its core) + money it receives
//   give     = (terms it gives (x core_give% for its cores) + money it pays) x give_weight%
//   relation = relation / max x relation; when the proposer is at least coerce_ratio% as strong and the
//              relation is bad, the bad relation counts for the proposer instead (x hostile%)
//   power    = (proposer strength / its strength - 1, at most power_cap%) x power, x neighbour% if they border
//   score    = gain - give + relation + power - reluctance
struct Score {
    int64_t gain = 0, give = 0, relation = 0, power = 0, total = 0;
};

int64_t strength(int country) {
    int land = 0, naval = 0;
    using SumFn = void(__stdcall*)(uintptr_t, int*, int*);
    fn<SumFn>(kUnitsSumStrength)(game::country(country) + kCountryArmies, &land, &naval);
    return static_cast<int64_t>(land) * 3 + naval;  // the peace offer's force balance: 0.75 land, 0.25 naval
}

bool is_core_of(uint32_t province_id, int country) {
    if (!province_id) {
        return false;
    }
    uintptr_t province = game::province(province_id);
    uintptr_t end = read<uintptr_t>(province + kProvinceCores + 4);
    for (uintptr_t c = read<uintptr_t>(province + kProvinceCores); c < end; c += 8) {
        if (read<int>(c + 4) == country) {
            return true;
        }
    }
    return false;
}

// Whether `self` cares about a term that benefits the third country: it borders it, leads its sphere or
// is its overlord, or the land the term moves holds cores of `self`.
bool has_stake(int self, int third, uintptr_t g) {
    uintptr_t t = game::country(third);
    uintptr_t neighbours = read<uintptr_t>(t + kCountryNeighbours);  // none for a country yet to be released
    if ((neighbours && read<uint8_t>(neighbours + self)) || read<int>(t + kCountrySphereLeader) == self) {
        return true;
    }
    if ((read<uint8_t>(t + kCountryIsVassal) || read<uint8_t>(t + kCountryIsSubstate)) &&
        read<int>(t + kCountryOverlord) == self) {
        return true;
    }
    uintptr_t cb = read<uintptr_t>(g + kGoalCb);
    std::vector<uintptr_t> states;
    if (read<uint8_t>(cb + kCbAllAllowedStates)) {
        states = target_states(g);
    } else if (uint32_t id = read<uint32_t>(g + kGoalProvince)) {
        states.push_back(read<uintptr_t>(game::province(id) + kProvinceState));
    }
    for (uintptr_t st : states) {
        for (uintptr_t p = read<uintptr_t>(st + kStateProvinceIds); p < read<uintptr_t>(st + kStateProvinceIds + 4); p += 4) {
            if (is_core_of(read<uint32_t>(p), self)) {
                return true;
            }
        }
    }
    return false;
}

Score score_details(uintptr_t action, int self) {
    const Config& k = g_config;
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    int other = self == a ? r : a;
    Score s;
    int64_t give = 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        if (!selected(action, i)) {
            continue;
        }
        uintptr_t g = goal(action, i);
        int64_t cost = static_cast<int64_t>(peace_cost(g)) * repeat_percent(action, i) / 1000;  // x1000 -> x100
        bool core = is_core_of(read<uint32_t>(g + kGoalProvince), self);
        if (read<int>(g + kGoalActorIndex) == self) {
            int third = read<int>(g + kGoalThirdIndex);
            if (third && !has_stake(self, third, g)) {
                cost = cost * k.treaty_ai_third_party / 100;  // it goes to someone the AI does not care about
            }
            s.gain += core ? cost * k.treaty_ai_core_gain / 100 : cost;
        } else if (read<int>(g + kGoalReceiverIndex) == self) {
            give += core ? cost * k.treaty_ai_core_give / 100 : cost;
        }
    }
    int64_t m = read<int64_t>(action + kMoney);  // actor -> recipient
    int64_t to_self = self == r ? m : -m;
    int64_t per_point = k.treaty_ai_money_per_point > 0 ? k.treaty_ai_money_per_point : 1;
    int64_t money_points = to_self * 100 / (per_point * kMoneyOne);
    if (money_points > 0) {
        s.gain += money_points;
    } else {
        give -= money_points;
    }
    s.give = give * k.treaty_ai_give_weight / 100;

    int64_t ours = strength(self);
    int64_t theirs = strength(other);
    int64_t ratio = (theirs + 1) * 100 / (ours + 1);  // percent: 300 = the proposer is three times as strong
    int value = read<int>(relation(self, other) + kRelationValue);
    int max = read<int>(hooks::exe_va(kRelationMax));
    s.relation = max > 0 ? static_cast<int64_t>(value) * k.treaty_ai_relation * 100 / max : 0;
    if (value < 0 && ratio >= k.treaty_ai_coerce_ratio) {
        s.relation = -s.relation * k.treaty_ai_hostile / 100;
    }
    int64_t over = ratio > 100 ? ratio - 100 : 0;
    if (over > k.treaty_ai_power_cap) {
        over = k.treaty_ai_power_cap;
    }
    s.power = over * k.treaty_ai_power;  // (over / 100) x power, in points x 100
    uintptr_t neighbour_table = read<uintptr_t>(game::country(other) + kCountryNeighbours);  // none: see has_stake
    bool neighbours = neighbour_table && read<uint8_t>(neighbour_table + self) != 0;
    if (neighbours) {
        s.power = s.power * k.treaty_ai_neighbour / 100;
    }
    s.total = s.gain - s.give + s.relation + s.power - static_cast<int64_t>(k.treaty_ai_reluctance) * 100;
    return s;
}

int score_for(uintptr_t action, int self) { return static_cast<int>(score_details(action, self).total); }

// The yesmen console command: the AI accepts everything (AI_EvaluateCommandChance honours it too).
bool yesmen() { return read<uint8_t>(hooks::exe_va(kCheatAiAcceptAll)) != 0; }

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
    *reinterpret_cast<int*>(action + kBudget) = 0;
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
    *reinterpret_cast<int*>(copy + kBudget) = read<int>(self + kBudget);
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

// A notice holds terms already applied: nothing to check, it is only shown.
bool __fastcall slot_can_perform(uintptr_t self, void*, bool) { return is_notice(self) || !treaty_problem(self); }

// A forced treaty needs no consent and no diplomat on the road.
bool __fastcall slot_auto_accept(uintptr_t self, void*) { return is_forced(self); }
bool __fastcall slot_travels(uintptr_t self, void*) { return !is_forced(self); }

// A pending action of the local player, shown once: a forced treaty in its pending list is the winner's
// turn to dismantle a loser, a notice the terms an AI imposed on it (both the editor); any other, the
// DefaultDialog box for the recipient.
void __fastcall slot_show_incoming(uintptr_t self, void*, uintptr_t owner) {
    if (is_forced(self) || is_notice(self)) {
        treaty_ui::show_forced(owner, self);
        return;
    }
    fn<void(__fastcall*)(uintptr_t, void*, uintptr_t)>(kShowIncoming)(self, nullptr, owner);
}

// Vassal_Release(&vassal, &overlord) as po_release_puppet calls it; ESP is restored here, so the
// convention does not matter.
void release_vassal(int vassal, int overlord) {
    uint32_t vassal_ref[2] = {read<uint32_t>(game::country(vassal) + kCountryTag), static_cast<uint32_t>(vassal)};
    uint32_t overlord_ref[2] = {read<uint32_t>(game::country(overlord) + kCountryTag), static_cast<uint32_t>(overlord)};
    uint32_t* v = vassal_ref;
    uint32_t* o = overlord_ref;
    uintptr_t f = hooks::exe_va(kVassalRelease);
    __asm {
        mov esi, esp
        push o
        push v
        call f
        mov esp, esi
    }
}

// The answer and CB texts go into the game's table when the CBs load (load_cbs) and when the GUI loads;
// the sim-side slots below make sure of it once more, the first time only (ensure_message_texts looks
// every key up and scans the localisation files for missing ones: not on every lockstep call).
void ensure_texts_once() {
    static bool done = false;
    if (!done) {
        done = true;
        treaty_ui::ensure_message_texts();
    }
}

void __fastcall slot_execute(uintptr_t self, void*, void*) {
    ensure_texts_once();
    int a = read<int>(self + kActorIndex);
    int r = read<int>(self + kRecipientIndex);
    if (is_notice(self)) {
        return;  // its terms were applied when the AI imposed them
    }
    if (Problem why = treaty_problem(self)) {
        logf("treaty: %d -> %d accepted but not applied: %s", a, r, why);
        return;
    }
    // A forced treaty costs infamy only for the points past its budget.
    bool forced = is_forced(self);
    if (forced) {
        int over = forced_points(self) - budget_of(self);
        if (over > 0) {
            int added = overflow_infamy(over);
            *reinterpret_cast<int*>(game::country(a) + kCountryInfamy) += added;
            logf("treaty: %d imposes %d points past its budget of %d on %d: +%d.%03d infamy", a, over, budget_of(self), r,
                 added / 1000, added % 1000);
        }
        if (int terms_infamy = forced_term_infamy(self)) {  // dismantle_infamy terms cost theirs anyway
            *reinterpret_cast<int*>(game::country(a) + kCountryInfamy) += terms_infamy;
            logf("treaty: %d takes land from %d: +%d.%03d infamy", a, r, terms_infamy / 1000, terms_infamy % 1000);
        }
    }
    size_t applied = 0;
    for (size_t i = 0, n = goal_count(self); i < n; ++i) {
        if (!selected(self, i)) {
            continue;
        }
        uintptr_t g = goal(self, i);
        if (Problem term_why = term_problem(self, g)) {  // an earlier term may have changed the map
            logf("treaty: term %u skipped: %s", static_cast<unsigned>(i), term_why);
            continue;
        }
        if (g_config.treaty_infamy && !forced) {
            uintptr_t gainer = game::country(read<int>(g + kGoalActorIndex));
            *reinterpret_cast<int*>(gainer + kCountryInfamy) += infamy(g);
        }
        uint8_t buffer[kWarGoalSize];
        uintptr_t run = puppet_goal(g, buffer);
        if (run != g && transfers_puppet(read<uintptr_t>(g + kGoalCb))) {
            release_vassal(read<int>(g + kGoalThirdIndex), read<int>(g + kGoalReceiverIndex));
        }
        if (is_partition_cb(read<uintptr_t>(g + kGoalCb))) {
            add_partition_cores(g);
        }
        fn<void(__stdcall*)(uintptr_t)>(kWarGoalExecute)(run);
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

void __fastcall slot_on_decline(uintptr_t, void*, void*) { ensure_texts_once(); }

// `token = value` in a save or command stream.
void write_number(uintptr_t stream, int token, int64_t number) {
    uintptr_t write_token = hooks::exe_va(kWriteToken);
    __asm {
        push stream
        mov ecx, token
        call write_token
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(number));
    std::string value = buf;
    GameString game_value(value);
    using StringFn = void(__fastcall*)(const GameString*, void*, uintptr_t, int);
    fn<StringFn>(kWriteString)(&game_value, nullptr, stream, 0);
}

void __fastcall slot_write_fields(uintptr_t self, void*, uintptr_t stream) {
    using WriteFn = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    fn<WriteFn>(kPeaceWriteFields)(self, nullptr, stream);
    if (int64_t m = read<int64_t>(self + kMoney)) {
        write_number(stream, g_money_token, m);
    }
    if (int budget = read<int>(self + kBudget); budget && g_budget_token) {
        write_number(stream, g_budget_token, budget);
    }
}

void __fastcall slot_on_keyword(uintptr_t self, void*, uintptr_t parser, int token) {
    if (g_money_token && token == g_money_token) {
        const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
        *reinterpret_cast<int64_t*>(self + kMoney) = std::strtoll(value, nullptr, 10);
        return;
    }
    if (g_budget_token && token == g_budget_token) {
        const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
        *reinterpret_cast<int*>(self + kBudget) = static_cast<int>(std::strtol(value, nullptr, 10));
        return;
    }
    using KeyFn = void(__fastcall*)(uintptr_t, void*, uintptr_t, int);
    fn<KeyFn>(kPeaceOnKeyword)(self, nullptr, parser, token);
}

// The chance AI_ProcessPendingProposals rolls against (0 or 100).
int __stdcall ai_chance(uintptr_t action) {
    ensure_texts_once();
    int r = read<int>(action + kRecipientIndex);
    int a = read<int>(action + kActorIndex);
    if (Problem why = treaty_problem(action)) {
        logf("treaty: AI %d refuses %d: %s", r, a, why);
        return 0;
    }
    Score s = score_details(action, r);
    bool accept = s.total >= 0 || yesmen();
    logf("treaty: AI %d %s %d: gain %lld - give %lld + relation %lld + power %lld - reluctance %d = %lld%s", r,
         accept ? "accepts" : "refuses", a, s.gain, s.give, s.relation, s.power, g_config.treaty_ai_reluctance * 100,
         s.total, yesmen() ? " (yesmen)" : "");
    return accept ? 100 : 0;
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
    g_transfer_puppet_token = add_token(kTransferPuppetKeyword);
    g_budget_token = add_token(kBudgetKeyword);
    g_free_token = add_token(kFreeKeyword);
    g_infamy_token = add_token(kInfamyKeyword);
    g_cost_token = add_token(kCostKeyword);
    g_repeat_token = add_token(kRepeatKeyword);
    g_dismantling_token = add_token(kDismantlingKeyword);
    g_to_third_token = add_token(kToThirdKeyword);
    g_partition_token = add_token(kPartitionKeyword);
    g_ai_weight_token = add_token(kAiWeightKeyword);
    logf("treaty: tokens %s 0x%X, %s 0x%X", kPartitionKeyword, g_partition_token, kAiWeightKeyword, g_ai_weight_token);
    logf("treaty: tokens %s 0x%X, %s 0x%X", kDismantlingKeyword, g_dismantling_token, kToThirdKeyword, g_to_third_token);
    logf("treaty: tokens %s 0x%X, %s 0x%X, %s 0x%X", kInfamyKeyword, g_infamy_token, kCostKeyword, g_cost_token,
         kRepeatKeyword, g_repeat_token);
    logf("treaty: tokens %s 0x%X, %s 0x%X, %s 0x%X, %s 0x%X, %s 0x%X, %s 0x%X", kKeyword, g_token, kActionKeyword,
         g_action_token, kMoneyKeyword, g_money_token, kTransferPuppetKeyword, g_transfer_puppet_token, kBudgetKeyword,
         g_budget_token, kFreeKeyword, g_free_token);
    for (DismantleEffect& d : g_dismantle_effects) {
        d.token = add_token(d.keyword);
        logf("treaty: effect %s 0x%X", d.keyword, d.token);
    }
}

// Replaces the CALL CBManager_Load(manager, block) (stdcall; it deletes the block): registers our words,
// then hands the next in the chain the block of cb_types.txt with the plugin's treaty CBs appended.
void __stdcall load_cbs(uintptr_t manager, uintptr_t block) {
    register_tokens();
    using LoadFn = void(__stdcall*)(uintptr_t, uintptr_t);
    uintptr_t merged = g_token ? treaty_ui::merged_cb_block() : 0;
    if (!merged) {
        reinterpret_cast<LoadFn>(g_prev_load)(manager, block);
    } else {
        reinterpret_cast<LoadFn>(g_prev_load)(manager, merged);
        if (block) {
            using DeleteFn = void(__fastcall*)(uintptr_t, void*, int);
            read<DeleteFn>(read<uintptr_t>(block))(block, nullptr, 1);
        }
    }
    // The treaty CBs' names and terms now, before the game can look one up: a key it looks up while
    // missing is inserted with the key as its text, and that one would then stay.
    treaty_ui::ensure_message_texts();
}

using OnKeywordFn = void(__fastcall*)(uintptr_t cb, void*, uintptr_t parser, int token);

void __fastcall on_keyword(uintptr_t cb, void*, uintptr_t parser, int token) {
    if (g_transfer_puppet_token && token == g_transfer_puppet_token) {
        if (read<int>(parser + kParserValueToken) == kTokenYes && !transfers_puppet(cb)) {
            g_transfer_puppet_cbs().push_back(cb);
            logf("treaty: %s hands over a puppet", GameString::copy(cb + kCbName).c_str());
        }
        return;
    }
    if (g_free_token && token == g_free_token) {
        if (read<int>(parser + kParserValueToken) == kTokenYes && !is_free_cb(cb)) {
            g_free_cbs().push_back(cb);
            logf("treaty: %s is free in a dismantle", GameString::copy(cb + kCbName).c_str());
        }
        return;
    }
    if (g_infamy_token && token == g_infamy_token) {
        if (read<int>(parser + kParserValueToken) == kTokenYes && !charges_infamy(cb)) {
            g_infamy_cbs().push_back(cb);
            logf("treaty: %s costs infamy in a dismantle", GameString::copy(cb + kCbName).c_str());
        }
        return;
    }
    if (g_dismantling_token && token == g_dismantling_token) {
        if (read<int>(parser + kParserValueToken) == kTokenYes && !is_dismantling_cb(cb)) {
            g_dismantling_cbs().push_back(cb);
            logf("treaty: %s may be imposed in a dismantle", GameString::copy(cb + kCbName).c_str());
        }
        return;
    }
    if (g_to_third_token && token == g_to_third_token) {
        if (read<int>(parser + kParserValueToken) == kTokenYes && !goes_to_third(cb)) {
            g_to_third_cbs().push_back(cb);
            logf("treaty: %s gives to its third country", GameString::copy(cb + kCbName).c_str());
        }
        return;
    }
    if (g_ai_weight_token && token == g_ai_weight_token) {
        const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
        int percent = static_cast<int>(std::strtod(value, nullptr) * 100 + 0.5);
        g_ai_weight_percent()[cb] = percent < 0 ? 0 : percent;
        logf("treaty: %s weighs %d%% to an AI dismantler", GameString::copy(cb + kCbName).c_str(), percent);
        return;
    }
    if (g_partition_token && token == g_partition_token) {
        if (read<int>(parser + kParserValueToken) == kTokenYes && !is_partition_cb(cb)) {
            g_partition_cbs().push_back(cb);
            logf("treaty: %s partitions by common/se_partitions.txt", GameString::copy(cb + kCbName).c_str());
        }
        return;
    }
    if (g_repeat_token && token == g_repeat_token) {
        const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
        int percent = static_cast<int>(std::strtod(value, nullptr) * 100 + 0.5);
        g_repeat_percent()[cb] = percent < 0 ? 0 : percent;
        logf("treaty: %s costs %d%% for each further term with the same country", GameString::copy(cb + kCbName).c_str(),
             percent);
        return;
    }
    if (g_cost_token && token == g_cost_token) {
        const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
        int percent = static_cast<int>(std::strtod(value, nullptr) * 100 + 0.5);
        g_cost_percent()[cb] = percent < 0 ? 0 : percent;
        logf("treaty: %s costs %d%% of its points in a dismantle", GameString::copy(cb + kCbName).c_str(), percent);
        return;
    }
    if (!g_token || token != g_token) {
        reinterpret_cast<OnKeywordFn>(g_prev_on_keyword)(cb, nullptr, parser, token);
        return;
    }
    if (read<int>(parser + kParserValueToken) == kTokenYes && !is_treaty_cb(cb)) {
        g_cbs().push_back(cb);
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
    set(13, reinterpret_cast<uintptr_t>(&slot_show_incoming));  // DefaultDialog, or the dismantle editor
    set_exe(14, 0x00912E70);  // DiplomaticAction_CreateDialog (never reached: not in the view's table)
    set(16, reinterpret_cast<uintptr_t>(&slot_offer_text));
    set_exe(17, 0x00633620);  // no third parties
    set_exe(18, 0x0092C720);  // SetRecipient: the base's, no war lookup
    set_exe(19, 0x0092C700);  // SetActor
    for (uint32_t slot = 20; slot <= 23; ++slot) {
        set_exe(slot, 0x00424B40);  // no diplomatic or influence cost, no relation change
    }
    set_exe(26, 0x00A31490);  // not while at war with the recipient
    set(27, reinterpret_cast<uintptr_t>(&slot_auto_accept));  // only a forced one
    set(28, reinterpret_cast<uintptr_t>(&slot_travels));      // diplomat travel time, but for a forced one
    set(32, reinterpret_cast<uintptr_t>(&slot_execute));
    set(34, reinterpret_cast<uintptr_t>(&slot_on_decline));  // declined: nothing but the message texts
    set(36, reinterpret_cast<uintptr_t>(&slot_can_perform));
    set_exe(37, 0x00928300);  // no "cannot send" text
}

}  // namespace

namespace treaty {

bool enabled() { return g_vtable != 0 && g_action_token != 0; }

const std::vector<uintptr_t>& treaty_cbs() { return g_cbs(); }

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
                       read<uint32_t>(g + kGoalProvince), read<int>(g + kGoalThirdIndex)});
    }
    return out;
}

int budget(uintptr_t action) { return budget_of(action); }

bool is_notice(uintptr_t action) { return se::features::is_notice(action); }

uintptr_t clone(uintptr_t action) { return slot_clone(action, nullptr); }

void set_budget(uintptr_t action, int points) { *reinterpret_cast<int*>(action + kBudget) = points; }

int points(uintptr_t action) { return forced_points(action); }

int infamy_past_budget(int over) { return over > 0 ? overflow_infamy(over) : 0; }

bool is_pending(int country, uintptr_t action) {
    for (uintptr_t node = read<uintptr_t>(game::country(country) + kCountryPending); node; node = read<uintptr_t>(node + 8)) {
        if (read<uintptr_t>(node) == action) {
            return true;
        }
    }
    return false;
}

void remove_pending(int country, uintptr_t action) {
    uintptr_t c = game::country(country);
    for (uintptr_t node = read<uintptr_t>(c + kCountryPending); node; node = read<uintptr_t>(node + 8)) {
        if (read<uintptr_t>(node) != action) {
            continue;
        }
        uintptr_t prev = read<uintptr_t>(node + 4);
        uintptr_t next = read<uintptr_t>(node + 8);
        if (prev) {
            *reinterpret_cast<uintptr_t*>(prev + 8) = next;
        } else {
            *reinterpret_cast<uintptr_t*>(c + kCountryPending) = next;
        }
        if (next) {
            *reinterpret_cast<uintptr_t*>(next + 4) = prev;
        } else {
            *reinterpret_cast<uintptr_t*>(c + kCountryPending + 4) = prev;
        }
        *reinterpret_cast<int*>(c + kCountryPending + 8) -= 1;
        game_delete(reinterpret_cast<void*>(node));
        using DtorFn = void(__fastcall*)(uintptr_t, void*, int);
        read<DtorFn>(read<uintptr_t>(action))(action, nullptr, 1);
        return;
    }
}

int64_t money(uintptr_t action) { return read<int64_t>(action + kMoney); }

void set_money(uintptr_t action, int64_t amount) { *reinterpret_cast<int64_t*>(action + kMoney) = amount; }

std::string problem(uintptr_t action) {
    Problem why = treaty_problem(action);
    return why ? text(why) : std::string();
}

bool can_send(uintptr_t action) {
    uintptr_t f = hooks::exe_va(kCanPerform);
    bool result;
    __asm {
        push esi
        mov esi, action
        push 0
        call f
        mov result, al
        pop esi
    }
    return result;
}

bool is_human(int country) { return se::features::is_human(country); }

bool possible(int actor, int recipient) {
    return actor != recipient && exists(actor) && exists(recipient) && !read<uintptr_t>(relation(actor, recipient) + kRelationWar);
}

int ai_score(uintptr_t action, int country) { return score_for(action, country); }

bool ai_accepts(uintptr_t action, int country) { return yesmen() || score_for(action, country) >= 0; }

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
    // As CWarGoalDialog_FillCbList: the scope is the target (who gives), THIS the one who gains.
    set_country(outer, gives);
    set_country(inner, gains);
    *reinterpret_cast<void**>(outer + kScopeThis) = inner;
    using CanUseFn = bool(__fastcall*)(uintptr_t, void*, void*);
    return read<CanUseFn>(read<uintptr_t>(cb) + kCbCanUse)(cb, nullptr, outer);
}

bool cb_needs_country(uintptr_t cb) {
    return read<uintptr_t>(cb + kCbAllowedCountriesChildren + 4) != read<uintptr_t>(cb + kCbAllowedCountriesChildren);
}

void log_country_conditions(uintptr_t cb, int gains, int gives, int third) {
    log_conditions_impl(cb, gains, gives, third);
}

bool country_allowed(uintptr_t cb, int gains, int gives, int third) {
    return scoped_allowed_country(cb, gains, gives, third);
}

bool cb_needs_province(uintptr_t cb) {
    bool states = read<uintptr_t>(cb + kCbAllowedStatesChildren + 4) != read<uintptr_t>(cb + kCbAllowedStatesChildren);
    return (states && !read<uint8_t>(cb + kCbAllAllowedStates)) || has_peace_option(cb, kPoDemandState);
}

bool cb_unsupported(uintptr_t cb) { return has_peace_option(cb, kPoAnnex) || has_peace_option(cb, kPoColony); }

namespace {

// check_term's test, as a key (see term_problem): what the sim path and the editor both use.
Problem check_term_key(uintptr_t action, const Term& term) {
    if (cb_unsupported(term.cb)) {
        return "SE_TREATY_TERM_ANNEX";
    }
    if (!cb_usable(term.cb, term.actor, term.receiver)) {
        return "SE_TREATY_TERM_INVALID";
    }
    // One term per state: a second one would find the map already changed when the treaty is applied.
    uintptr_t state = term.province ? read<uintptr_t>(game::province(term.province) + kProvinceState) : 0;
    for (size_t i = 0, n = goal_count(action); i < n; ++i) {
        uintptr_t other = goal(action, i);
        uint32_t id = read<uint32_t>(other + kGoalProvince);
        bool same_state = state && id && read<uintptr_t>(game::province(id) + kProvinceState) == state;
        bool same_term = read<uintptr_t>(other + kGoalCb) == term.cb && read<int>(other + kGoalActorIndex) == term.actor &&
                         id == term.province && read<int>(other + kGoalThirdIndex) == term.third;
        if (same_state || same_term) {
            return "SE_TREATY_TERM_DUPLICATE";
        }
    }
    uint8_t g[kWarGoalSize];
    build_goal(g, term);
    return term_problem(action, reinterpret_cast<uintptr_t>(g));
}

}  // namespace

bool term_allowed(uintptr_t action, const Term& term) { return !check_term_key(action, term); }

std::string check_term(uintptr_t action, const Term& term) {
    Problem why = check_term_key(action, term);
    return why ? text(why) : std::string();
}

bool is_free(uintptr_t cb) { return is_free_cb(cb); }

bool is_dismantling(uintptr_t cb) { return is_dismantling_cb(cb); }

bool costs_infamy_when_forced(uintptr_t cb) { return charges_infamy(cb); }

int forced_cost(const Term& term) {
    uint8_t g[kWarGoalSize];
    build_goal(g, term);
    return dismantle_points(reinterpret_cast<uintptr_t>(g));
}

int forced_terms_infamy(uintptr_t action) { return forced_term_infamy(action); }

int term_points(uintptr_t action, size_t index) { return term_points_at(action, index, is_forced(action)); }

void add_free_terms(uintptr_t action) {
    int a = read<int>(action + kActorIndex);
    int r = read<int>(action + kRecipientIndex);
    for (uintptr_t cb : g_cbs()) {
        // Only terms that need no choice: no third country, no state to pick.
        if (!is_free_cb(cb) || !is_dismantling_cb(cb) || cb_unsupported(cb) || cb_needs_country(cb) || cb_needs_province(cb) || !cb_usable(cb, a, r)) {
            continue;
        }
        Term term{cb, a, r, 0, 0};
        if (term_allowed(action, term)) {
            add_term(action, term);
        }
    }
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

namespace {

// --- Dismantling ---------------------------------------------------------------------------------
//
// The script effects `dismantle = TAG` (the scope dismantles TAG) and `dismantled_by = TAG` (TAG dismantles
// the scope), each with a _gw form (treaty_dismantle_gw% of the points); TAG may be THIS or FROM. The
// dismantler imposes a forced treaty on the other. Its budget of war-score points depends on the
// dismantled: the leader of a war against the dismantler, a great power, a secondary power, the rest.
// Points past it cost the dismantler infamy (overflow_infamy). The turn waits while the two are at war
// (in a CB's on_po_accepted the peace has not ended the war yet) and is taken when a peace ends it, else
// at once. A human dismantler gets it as a pending action of its own, shown as the treaty editor
// (treaty_ui::show_forced), one at a time; what it sends is accepted at once. An AI dismantler picks its
// terms here (effects and peaces run on every machine, and the picking reads synced state only).
//
// The effect: the factory every effect block uses for its children (effect slot 4) builds a
// release_vassal effect for our tokens (its parse reads TAG / THIS / FROM), then gives it our vtable, a
// copy of CReleaseVassalEffect's with Execute and the tooltip replaced. docs/re/systems/effects.md.

struct Dismantle {
    int winner;
    int loser;
    int budget;
};

std::vector<Dismantle>& g_dismantles() { static std::vector<Dismantle> v; return v; }  // turns waiting for the peace

// One turn per loser: a great-war peace runs the obligatory CB's on_po_accepted once per goal, and a
// mod's dismantle CB may be in the same peace. A second effect on a waiting loser only raises the budget.
void queue_dismantle(int winner, int loser, int budget) {
    for (Dismantle& d : g_dismantles()) {
        if (d.loser == loser) {
            d.budget = d.budget < budget ? budget : d.budget;
            logf("treaty: %d is already to be dismantled by %d; %d points now", loser, d.winner, d.budget);
            return;
        }
    }
    g_dismantles().push_back({winner, loser, budget});
}
uintptr_t g_peace_execute = 0;
uintptr_t g_effect_vtable_storage[kDismantleEffects][kEffectSlots + 1];  // per effect: [0] = RTTI locator, then the slots

bool is_great_power(int country) { return read<uint8_t>(game::country(country) + kCountryGreatPower) != 0; }

// The two hold an alliance: the agreement object at Relation+0x28, which War_CanDeclare checks (I: owned
// by one side, so both directions are read).
bool allied(int a, int b) {
    return read<uintptr_t>(relation(a, b) + kRelationAlliance) != 0 ||
           read<uintptr_t>(relation(b, a) + kRelationAlliance) != 0;
}

// is_secondary_power: civilized, not a great power, ranked up to the define.
bool is_secondary_power(int country) {
    uintptr_t c = game::country(country);
    uintptr_t defines = read<uintptr_t>(read<uintptr_t>(hooks::exe_va(kEconomyDefines)) + 0x40);
    return read<uint8_t>(c + kCountryCivilized) && !read<uint8_t>(c + kCountryGreatPower) &&
           read<int>(c + kCountryRank) <= read<int>(defines + 0x50) / 1000;
}

// The leader of either side of the war between the two (the first of the side).
bool leads_war_against(int loser, int winner) {
    uintptr_t war = read<uintptr_t>(relation(winner, loser) + kRelationWar);
    if (!war) {
        return false;
    }
    for (uint32_t side : {kWarAttackers, kWarDefenders}) {
        uintptr_t first = read<uintptr_t>(war + side);
        if (first != read<uintptr_t>(war + side + 4) && read<int>(first + 4) == loser) {
            return true;
        }
    }
    return false;
}

int dismantle_budget(int winner, int loser, bool gw) {
    const Config& k = g_config;
    int points = leads_war_against(loser, winner) ? k.treaty_dismantle_leader
                 : is_great_power(loser)          ? k.treaty_dismantle_gp
                 : is_secondary_power(loser)      ? k.treaty_dismantle_secondary
                                                  : k.treaty_dismantle_other;
    return gw ? points * k.treaty_dismantle_gw / 100 : points;
}

void delete_treaty(uintptr_t action) {
    using DtorFn = void(__fastcall*)(uintptr_t, void*, int);
    read<DtorFn>(read<uintptr_t>(action))(action, nullptr, 1);
}

// The states a term moves from the loser (po_demand_state, po_transfer_provinces).
std::vector<uintptr_t> states_moved(const treaty::Term& term) {
    // po_demand_province (the plugin's) moves one province: its state stands for it in the land rules
    if (!has_peace_option(term.cb, kPoDemandState) && !has_peace_option(term.cb, kPoTransferProvinces) &&
        !treaty_ui::takes_one_province(term.cb)) {
        return {};
    }
    if (!read<uint8_t>(term.cb + kCbAllAllowedStates)) {
        uintptr_t state = term.province ? read<uintptr_t>(game::province(term.province) + kProvinceState) : 0;
        return state ? std::vector<uintptr_t>{state} : std::vector<uintptr_t>{};
    }
    uint8_t g[kWarGoalSize];
    build_goal(g, term);
    return target_states(reinterpret_cast<uintptr_t>(g));
}

// Percent of a CB's value to an AI dismantler: its `ai_dismantle_weight` (default 100).
int ai_weight(uintptr_t cb) {
    auto it = g_ai_weight_percent().find(cb);
    return it == g_ai_weight_percent().end() ? 100 : it->second;
}

// Appends `action` to the country's pending list; the game shows it through slot 13 when the country is
// the local player (Country_AddPendingAction appends for the local player only).
void add_pending(int country, uintptr_t action) {
    uintptr_t c = game::country(country);
    uintptr_t f = hooks::exe_va(kAddPendingAction);
    __asm {
        push esi
        push edi
        mov esi, c
        mov edi, action
        call f
        pop edi
        pop esi
    }
}

// A human winner's turn on one loser: a forced treaty in its own pending list, which the game shows to it
// through slot 13 (on its machine only, as Country_AddPendingAction does for any pending action).
void invite(int winner, int loser, int budget) {
    uintptr_t action = treaty::create(winner, loser);
    if (!action) {
        return;
    }
    treaty::set_budget(action, budget);
    add_pending(winner, action);
    logf("treaty: %d may dismantle %d (%d points)", winner, loser, budget);
}

// Takes every waiting turn whose two countries are at peace, in the order the effects ran.
void take_dismantles() {
    std::vector<Dismantle> waiting;
    std::vector<Dismantle> due;
    for (const Dismantle& d : g_dismantles()) {
        (read<uintptr_t>(relation(d.winner, d.loser) + kRelationWar) ? waiting : due).push_back(d);
    }
    g_dismantles().swap(waiting);
    int local = read<int>(game::world() + kWorldLocalPlayer);
    for (const Dismantle& d : due) {
        if (!exists(d.winner) || !exists(d.loser)) {
            continue;
        }
        if (!is_human(d.winner)) {
            treaty_ai::impose(g_config, d.winner, d.loser, d.budget);
        } else if (local == d.winner) {
            invite(d.winner, d.loser, d.budget);
        }
    }
}

// The effect's target (TAG, THIS or FROM), as CReleaseVassalEffect resolves it (0x0088E620), without its
// random-vassal case: THIS and FROM are read from the THIS scope (scope+0x24), a country or else the owner
// of its province.
int effect_target(uintptr_t effect, uintptr_t scope) {
    uintptr_t this_scope = read<uintptr_t>(scope + kScopeThis);
    auto owner = [](uint32_t province) { return province ? read<int>(game::province(province) + kProvinceOwner) : 0; };
    if (read<uint8_t>(effect + kEffectUsesThis)) {
        int index = read<int>(this_scope + kScopeCountryIndex);
        return index ? index : owner(read<uint32_t>(this_scope + kScopeThisProvince));
    }
    if (read<uint8_t>(effect + kEffectUsesFrom)) {
        int index = read<int>(this_scope + kScopeFromIndex);
        return index ? index : owner(read<uint32_t>(this_scope + kScopeFromProvince));
    }
    return read<int>(effect + kEffectTargetIndex);
}

// Which of the dismantle effects `effect` is, by its vtable.
const DismantleEffect& effect_kind(uintptr_t effect) {
    for (size_t i = 0; i < kDismantleEffects; ++i) {
        if (read<uintptr_t>(effect) == reinterpret_cast<uintptr_t>(&g_effect_vtable_storage[i][1])) {
            return g_dismantle_effects[i];
        }
    }
    return g_dismantle_effects[0];
}

// The two countries of the effect: dismantle: the scope dismantles the target; dismantled_by: the reverse.
void effect_sides(uintptr_t effect, uintptr_t scope, int& winner, int& loser) {
    int here = read<int>(scope + kScopeCountryIndex);
    int target = effect_target(effect, scope);
    bool reversed = effect_kind(effect).reversed;
    winner = reversed ? target : here;
    loser = reversed ? here : target;
}

// Effect slot 11, Execute: thiscall (scope, ?), RET 8.
void __fastcall dismantle_execute(uintptr_t effect, void*, uintptr_t scope, int) {
    int winner = 0, loser = 0;
    effect_sides(effect, scope, winner, loser);
    if (winner <= 0 || loser <= 0 || winner == loser || !exists(winner) || !exists(loser)) {
        logf("treaty: %s effect without two countries (%d on %d)", effect_kind(effect).keyword, winner, loser);
        return;
    }
    int budget = dismantle_budget(winner, loser, effect_kind(effect).gw);
    queue_dismantle(winner, loser, budget);
    logf("treaty: %d is to dismantle %d (%d points)%s", winner, loser, budget,
         read<uintptr_t>(relation(winner, loser) + kRelationWar) ? ", after the peace" : "");
    take_dismantles();
}

std::once_flag g_effect_texts_once;
using EffectTexts = std::map<std::string, std::string>;
EffectTexts& g_effect_texts() { static EffectTexts v; return v; }  // the tags' names and SE_DISMANTLE_EFFECT

const std::map<std::string, std::string>& effect_texts() {
    std::call_once(g_effect_texts_once, [] {
        std::vector<std::string> keys{"SE_DISMANTLE_EFFECT"};
        int count = read<int>(read<uintptr_t>(hooks::exe_va(game::kCountryManager)));
        for (int c = 1; c < count; ++c) {
            uint32_t tag = read<uint32_t>(game::country(c) + kCountryTag);
            keys.emplace_back(reinterpret_cast<const char*>(&tag), 3);
        }
        g_effect_texts() = game::read_localisation(keys);
    });
    return g_effect_texts();
}

std::string effect_text(const std::string& key) {
    auto it = effect_texts().find(key);
    return it == effect_texts().end() ? key : it->second;
}

std::string tag_name(int index) {
    if (index <= 0) {
        return "?";
    }
    uint32_t tag = read<uint32_t>(game::country(index) + kCountryTag);
    return effect_text(std::string(reinterpret_cast<const char*>(&tag), 3));
}

// The country with tag `word` (any case), or 0.
int country_by_tag(std::string word) {
    if (word.size() != 3) {
        return 0;
    }
    for (char& ch : word) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    int count = read<int>(read<uintptr_t>(hooks::exe_va(game::kCountryManager)));
    for (int c = 1; c < count; ++c) {
        uint32_t tag = read<uint32_t>(game::country(c) + kCountryTag);
        if (std::memcmp(&tag, word.data(), 3) == 0) {
            return c;
        }
    }
    return 0;
}

// The console command `dismantle <TAG> [<TAG>] [gw]`, for testing: the second country (the local player
// when left out) dismantles the first, as the dismantle (or dismantle_gw) effect would. On this machine
// only: in multiplayer it desyncs, as the game's own cheats do.
std::string console_dismantle(const std::vector<std::string>& args, bool& ok) {
    ok = false;
    std::vector<std::string> tags;
    bool gw = false;
    for (const std::string& a : args) {
        if (_stricmp(a.c_str(), "gw") == 0) {
            gw = true;
        } else {
            tags.push_back(a);
        }
    }
    if (tags.empty() || tags.size() > 2) {
        return "Usage: dismantle <loser TAG> [<winner TAG>] [gw]";
    }
    int loser = country_by_tag(tags[0]);
    int winner = tags.size() == 2 ? country_by_tag(tags[1]) : read<int>(game::world() + kWorldLocalPlayer);
    if (loser <= 0 || winner <= 0 || winner == loser || !exists(winner) || !exists(loser)) {
        return "Needs two different existing countries";
    }
    int budget = dismantle_budget(winner, loser, gw);
    queue_dismantle(winner, loser, budget);
    bool war = read<uintptr_t>(relation(winner, loser) + kRelationWar) != 0;
    logf("treaty: console: %d is to dismantle %d (%d points)%s", winner, loser, budget, war ? ", after the peace" : "");
    take_dismantles();
    ok = true;
    return tag_name(winner) + " dismantles " + tag_name(loser) + ": " + std::to_string(budget) + " points" +
           (war ? ", after the peace" : "");
}

// Effect slot 9, the tooltip: thiscall (std::string* out, scope, std::string by value, int), RET 0x28.
// `out` is uninitialised (the result), and the callee destroys the string it got by value.
uintptr_t __fastcall dismantle_tooltip(uintptr_t effect, void*, uintptr_t out, uintptr_t scope, GameString by_value, int) {
    int winner = 0, loser = 0;
    effect_sides(effect, scope, winner, loser);
    int budget = winner > 0 && loser > 0 && winner != loser ? dismantle_budget(winner, loser, effect_kind(effect).gw) : 0;
    std::string text = effect_text("SE_DISMANTLE_EFFECT");
    text = replace(replace(replace(text, "$WINNER$", tag_name(winner)), "$COUNTRY$", tag_name(loser)), "$POINTS$",
                   std::to_string(budget));
    make_game_string(out, text);
    if (by_value.capacity > 15) {
        game_delete(const_cast<char*>(by_value.ptr));
    }
    return out;
}

// The effect factory's builder (game/effects.h): our tokens become a release_vassal effect with our vtable.
bool build_dismantle(uintptr_t block, uintptr_t parser, int token, uintptr_t& result) {
    size_t kind = kDismantleEffects;
    for (size_t i = 0; i < kDismantleEffects; ++i) {
        if (g_dismantle_effects[i].token && token == g_dismantle_effects[i].token) {
            kind = i;
        }
    }
    if (kind == kDismantleEffects) {
        return false;
    }
    uintptr_t effect = game::build_template(block, parser, kTokenReleaseVassal, hooks::exe_va(kReleaseVassalEffectVtable), result);
    if (effect) {
        *reinterpret_cast<uintptr_t*>(effect) = reinterpret_cast<uintptr_t>(&g_effect_vtable_storage[kind][1]);
        logf("treaty: %s effect read", g_dismantle_effects[kind].keyword);
    } else {
        logf("treaty: a dismantle effect could not be built");
    }
    return true;
}

// Replaces CPeaceAction slot 32: the turns waiting for this peace are taken once it has ended the war.
void __fastcall peace_execute(uintptr_t self, void*, void* context) {
    reinterpret_cast<void(__fastcall*)(uintptr_t, void*, void*)>(g_peace_execute)(self, nullptr, context);
    if (!g_dismantles().empty()) {
        take_dismantles();
    }
}

void install_dismantle() {
    uintptr_t base = hooks::exe_va(kReleaseVassalEffectVtable);
    for (auto& storage : g_effect_vtable_storage) {
        game::copy_effect_vtable(base, storage, reinterpret_cast<uintptr_t>(&dismantle_tooltip),
                                 reinterpret_cast<uintptr_t>(&dismantle_execute));
    }
    if (!game::add_effect_builder(&build_dismantle)) {
        logf("treaty: no effect factory hook; no dismantle effect");
        return;
    }
    g_peace_execute = hooks::exe_va(kPeaceExecute);
    if (!hooks::replace_pointer(hooks::exe_va(kPeaceExecuteSlot), g_peace_execute, reinterpret_cast<uintptr_t>(&peace_execute))) {
        logf("treaty: CPeaceAction's Execute slot could not be replaced; dismantling waits for no peace");
    }
    logf("treaty: dismantle, dismantle_gw, dismantled_by, dismantled_by_gw effects on");
    if (game::add_console_command("dismantle", "Dismantles a country (Script Extender, for testing)",
                                  {"loser TAG", "<winner TAG>", "<gw>"}, &console_dismantle)) {
        logf("treaty: console command dismantle <TAG> [<TAG>] [gw] on");
    }
}

}  // namespace

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
    // Both sites are chained: demand_province (installed first) may hold them already, and is then
    // called through. The slot is chained the same way as the call: whatever is there is kept and logged
    // when it is not the vanilla method.
    uintptr_t slot = hooks::exe_va(kCbOnKeywordSlot);
    g_prev_on_keyword = read<uintptr_t>(slot);
    if (!g_prev_on_keyword ||
        !hooks::replace_pointer(slot, g_prev_on_keyword, reinterpret_cast<uintptr_t>(&on_keyword))) {
        logf("treaty: CCasusBelliType's OnKeyword slot at %08X could not be replaced; off", kCbOnKeywordSlot);
        return;
    }
    if (g_prev_on_keyword != hooks::exe_va(kCbOnKeyword)) {
        logf("treaty: the OnKeyword slot at %08X was already replaced; chaining to it", kCbOnKeywordSlot);
    }
    if (!hooks::chain_call_logged("treaty", kCbManagerLoadCall, kCbManagerLoad, reinterpret_cast<uintptr_t>(&load_cbs),
                                  g_prev_load)) {
        return;
    }
    install_dismantle();
    logf("treaty: on (is_treaty_cb in cb_types.txt)");
}

}  // namespace se::features

// For the AI dismantler (treaty_ai_dismantle.cpp).
namespace se::features::treaty {

int ai_weight(uintptr_t cb) { return se::features::ai_weight(cb); }
bool goes_to_third(uintptr_t cb) { return se::features::goes_to_third(cb); }
bool is_partition(uintptr_t cb) { return se::features::is_partition_cb(cb); }
bool of_interest(const std::vector<uintptr_t>& states, int country) { return se::features::of_interest(states, country); }
bool province_of_interest(uint32_t province, int country) { return se::features::province_of_interest(province, country); }
std::vector<uintptr_t> states_moved(const Term& term) { return se::features::states_moved(term); }
bool is_core_of(uint32_t province, int country) { return se::features::is_core_of(province, country); }
bool allied(int a, int b) { return se::features::allied(a, b); }
bool is_great_power(int country) { return se::features::is_great_power(country); }
bool is_secondary_power(int country) { return se::features::is_secondary_power(country); }
int infamy_x1000(int country) { return read<int>(game::country(country) + kCountryInfamy); }
std::string tag_name(int country) { return se::features::tag_name(country); }
size_t goal_count(uintptr_t action) { return se::features::goal_count(action); }

int term_infamy(const Term& term) {
    uint8_t g[kWarGoalSize];
    build_goal(g, term);
    return infamy(reinterpret_cast<uintptr_t>(g));
}

void show_to_local_human(uintptr_t action, int country) {
    if (!is_human(country) || read<int>(game::world() + kWorldLocalPlayer) != country) {
        return;
    }
    uintptr_t notice = slot_clone(action, nullptr);
    if (notice) {
        *reinterpret_cast<int*>(notice + kBudget) = kNoticeBudget;
        add_pending(country, notice);
    }
}

void execute(uintptr_t action) {
    slot_execute(action, nullptr, nullptr);
    delete_treaty(action);
}

}  // namespace se::features::treaty
