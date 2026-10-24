#include "features/demand_province.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Peace options and war goals: docs/re/systems/casus-belli.md ("Peace options", "po_demand_province").
//
// - The keyword: right before CBManager_Load, `po_demand_province` is registered as a dynamic lexer token
//   (Token_AddDynamic, as buildings.txt does for building names). CCasusBelliType's vtable slot 4
//   (OnKeyword) gets a wrapper: for that token with `yes` it adds the po_demand_state peace option (once)
//   and marks the CB.
// - For a war goal of a marked CB, in WarGoal_Execute the province transfer gets a one-element list (the
//   war goal's province) instead of the state's, and the whole country is annexed only when that province
//   is all it owns; in WarGoal_PeaceCost the per-province sum covers that province only.

namespace se::features {

namespace {

using game::GameString;
using game::read;

constexpr uint32_t kTokenAddDynamic = 0x009902D0;  // cdecl (const std::string* name, bool allowExisting) -> id
constexpr uint32_t kLexerReady = 0x01258A64;       // u8: the lexer's keyword table is built
constexpr uint32_t kCbManagerLoad = 0x0090CDC0;
constexpr uint32_t kCbManagerLoadCall = 0x0063DCD2;
constexpr uint32_t kCbOnKeywordSlot = 0x00E33734;  // CCasusBelliType_vftable slot 4
constexpr uint32_t kCbOnKeyword = 0x0090A1E0;      // thiscall (cb; parser, key token), RET 8
constexpr uint32_t kTransferProvinces = 0x004B5A90;  // cdecl (vector<int>* ids, receiver ref, date*, 1, cores ref)
constexpr uint32_t kTransferCall = 0x0094E844;       // WarGoal_Execute, po_demand_state: the state's provinces
constexpr uint32_t kAnnexCheck = 0x0094E77B;         // WarGoal_Execute: CMP [owner + 0xE4C], 1 (one state left)
constexpr uint8_t kAnnexCheckBytes[] = {0x83, 0xBA, 0x4C, 0x0E, 0x00, 0x00, 0x01};
constexpr uint32_t kCostLoop = 0x009519AD;           // WarGoal_PeaceCost: MOV ESI, [EAX+0x48]; MOV [ESP+0x14], EAX
constexpr uint8_t kCostLoopBytes[] = {0x8B, 0x70, 0x48, 0x89, 0x44, 0x24, 0x14};

constexpr const char* kKeyword = "po_demand_province";
constexpr int kPoDemandState = 0x8A3;
constexpr int kTokenYes = 0x29F;
constexpr uint32_t kParserValueToken = 0x228;  // token of the value just read

// CCasusBelliType
constexpr uint32_t kCbName = 0x14;          // std::string
constexpr uint32_t kCbPeaceOptions = 0x120;  // list of nodes {token, prev, next, u8}
constexpr uint32_t kNodeNext = 0x08;
// CWarGoal
constexpr uint32_t kWarGoalCb = 0x0C;
constexpr uint32_t kWarGoalProvince = 0x28;
// CCountry, CProvince, CState
constexpr uint32_t kCountryStateCount = 0xE4C;
constexpr uint32_t kProvinceState = 0x188;
constexpr uint32_t kStateProvinceIds = 0x48;  // vector<int>

int g_token = 0;                     // the keyword's token, once registered
std::vector<uintptr_t> g_cbs;        // CCasusBelliTypes with the keyword; written while cb_types.txt loads
uintptr_t g_cb_manager_load = 0;     // resolved
uintptr_t g_transfer_provinces = 0;  // resolved
uintptr_t g_cost_back = 0;           // resolved kCostLoop + 7
int g_annex_states = 0;              // compared with 1 by the annex check; peace runs on the main thread

// A province list for the game to read: vector<int> {begin, end, capacity}.
struct ProvinceList {
    const int* begin;
    const int* end;
    const int* capacity;
    int id;
};

// A stand-in CState for the cost loop, which reads only its province list.
struct CostState {
    uint8_t unused[kStateProvinceIds];
    const int* begin;
    const int* end;
    int id;
};

thread_local ProvinceList t_transfer;
thread_local CostState t_cost;  // the AI prices war goals too, maybe off the main thread

bool is_province_cb(uintptr_t cb) {
    for (uintptr_t c : g_cbs) {
        if (c == cb) {
            return true;
        }
    }
    return false;
}

bool has_peace_option(uintptr_t cb, int token) {
    for (uintptr_t n = read<uintptr_t>(cb + kCbPeaceOptions); n; n = read<uintptr_t>(n + kNodeNext)) {
        if (read<int>(n) == token) {
            return true;
        }
    }
    return false;
}

void __stdcall register_token() {
    if (g_token) {
        return;
    }
    if (!read<uint8_t>(hooks::exe_va(kLexerReady))) {
        logf("demand_province: the lexer is not ready before cb_types.txt; %s stays unknown", kKeyword);
        return;
    }
    std::string name = kKeyword;
    GameString game_name(name);
    using AddFn = int(__cdecl*)(const GameString*, bool);
    g_token = reinterpret_cast<AddFn>(hooks::exe_va(kTokenAddDynamic))(&game_name, true);
    logf("demand_province: %s is token 0x%X", kKeyword, g_token);
}

// Replaces CALL CBManager_Load.
__declspec(naked) void load_thunk() {
    __asm {
        SE_SAVE_ALL
        call register_token
        SE_RESTORE_ALL
        jmp g_cb_manager_load
    }
}

using OnKeywordFn = void(__fastcall*)(uintptr_t cb, void*, uintptr_t parser, int token);

void __fastcall on_keyword(uintptr_t cb, void*, uintptr_t parser, int token) {
    auto original = reinterpret_cast<OnKeywordFn>(hooks::exe_va(kCbOnKeyword));
    bool ours = g_token && token == g_token;
    if (!ours && token != kPoDemandState) {
        original(cb, nullptr, parser, token);
        return;
    }
    if (read<int>(parser + kParserValueToken) != kTokenYes) {
        return;  // as the original does for every po_* that is not `yes`
    }
    if (ours && !is_province_cb(cb)) {
        g_cbs.push_back(cb);
        logf("demand_province: %s takes one province", GameString::copy(cb + kCbName).c_str());
    }
    if (!has_peace_option(cb, kPoDemandState)) {  // both keywords in one CB: a single option
        original(cb, nullptr, parser, kPoDemandState);
    }
}

// The province list WarGoal_Execute hands to the transfer: the state's, or the war goal's province.
const void* __stdcall transfer_list(uintptr_t war_goal, const void* state_provinces) {
    if (!is_province_cb(read<uintptr_t>(war_goal + kWarGoalCb))) {
        return state_provinces;
    }
    t_transfer.id = read<int>(war_goal + kWarGoalProvince);
    t_transfer.begin = &t_transfer.id;
    t_transfer.end = t_transfer.capacity = &t_transfer.id + 1;
    return &t_transfer;
}

// Replaces CALL TransferProvinces in WarGoal_Execute; EBX = war goal.
__declspec(naked) void transfer_thunk() {
    __asm {
        SE_SAVE_ALL
        mov eax, [esp + SE_SAVED_ALL_BYTES + 4]
        push eax
        push ebx
        call transfer_list
        mov [esp + SE_SAVED_ALL_BYTES + 4], eax
        SE_RESTORE_ALL
        jmp g_transfer_provinces
    }
}

// What the annex check compares with 1: the owner's state count, or for one province 1 only when that
// province is all the owner has.
int __stdcall annex_value(uintptr_t war_goal, uintptr_t owner, uintptr_t province) {
    int states = read<int>(owner + kCountryStateCount);
    if (!is_province_cb(read<uintptr_t>(war_goal + kWarGoalCb))) {
        return states;
    }
    uintptr_t state = read<uintptr_t>(province + kProvinceState);
    return states == 1 && game::vector_at(state + kStateProvinceIds).size() == 1 ? 1 : 0;
}

// Replaces CMP [EDX+0xE4C], 1 (CALL + 2 NOPs); EBX = war goal, EDX = owner, ESI = province. The flags
// are the result.
__declspec(naked) void annex_thunk() {
    __asm {
        SE_SAVE_ALL
        push esi
        push edx
        push ebx
        call annex_value
        mov g_annex_states, eax
        SE_RESTORE_ALL
        cmp g_annex_states, 1
        ret
    }
}

// The state the cost loop walks: the real one, or a stand-in holding the war goal's province.
uintptr_t __stdcall cost_state(uintptr_t war_goal, uintptr_t state) {
    if (!is_province_cb(read<uintptr_t>(war_goal + kWarGoalCb))) {
        return state;
    }
    t_cost.id = read<int>(war_goal + kWarGoalProvince);
    t_cost.begin = &t_cost.id;
    t_cost.end = &t_cost.id + 1;
    return reinterpret_cast<uintptr_t>(&t_cost);
}

// Jumped to from kCostLoop; EAX = state, ESI = war goal. Runs the two replaced instructions on the
// chosen state.
__declspec(naked) void cost_thunk() {
    __asm {
        SE_SAVE_ALL
        push eax
        push esi
        call cost_state
        mov [esp + SE_SAVED_EAX], eax
        SE_RESTORE_ALL
        mov esi, [eax + 0x48]
        mov [esp + 0x14], eax
        jmp g_cost_back
    }
}

bool install_annex_check() {
    uintptr_t at = hooks::exe_va(kAnnexCheck);
    if (!hooks::expect_bytes(at, kAnnexCheckBytes, sizeof(kAnnexCheckBytes))) {
        return false;
    }
    uint8_t patch[sizeof(kAnnexCheckBytes)] = {0xE8, 0, 0, 0, 0, 0x90, 0x90};
    auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&annex_thunk) - (at + 5));
    std::memcpy(patch + 1, &rel, sizeof(rel));
    return hooks::write_bytes(at, patch, sizeof(patch));
}

}  // namespace

void install_demand_province(const Config& config) {
    if (!config.demand_province) {
        return;
    }
    g_cb_manager_load = hooks::exe_va(kCbManagerLoad);
    g_transfer_provinces = hooks::exe_va(kTransferProvinces);
    g_cost_back = hooks::exe_va(kCostLoop) + sizeof(kCostLoopBytes);

    // The peace side first: inert until a CB is marked, so the keyword is only enabled when it all holds.
    if (!hooks::redirect_call(hooks::exe_va(kTransferCall), g_transfer_provinces,
                              reinterpret_cast<uintptr_t>(&transfer_thunk))) {
        logf("demand_province: the province transfer at %08X is not the expected one; off", kTransferCall);
        return;
    }
    if (!install_annex_check()) {
        logf("demand_province: the annex check at %08X is not the expected one; keyword off",
             kAnnexCheck);
        return;
    }
    if (!hooks::jump_hook(hooks::exe_va(kCostLoop), kCostLoopBytes, sizeof(kCostLoopBytes),
                          reinterpret_cast<uintptr_t>(&cost_thunk))) {
        logf("demand_province: the peace cost loop at %08X is not the expected one; keyword off", kCostLoop);
        return;
    }
    if (!hooks::replace_pointer(hooks::exe_va(kCbOnKeywordSlot), hooks::exe_va(kCbOnKeyword),
                                reinterpret_cast<uintptr_t>(&on_keyword))) {
        logf("demand_province: CCasusBelliType's OnKeyword slot is not the expected one; keyword off");
        return;
    }
    if (!hooks::redirect_call(hooks::exe_va(kCbManagerLoadCall), g_cb_manager_load,
                              reinterpret_cast<uintptr_t>(&load_thunk))) {
        logf("demand_province: the CBManager_Load call at %08X is not the expected one; keyword off",
             kCbManagerLoadCall);
        return;
    }
    logf("demand_province: on (%s in cb_types.txt)", kKeyword);
}

}  // namespace se::features
