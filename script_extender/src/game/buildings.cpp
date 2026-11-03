#include "game/buildings.h"

#include "game/objects.h"
#include "game/script.h"
#include "hooks/memory.h"

namespace se::game {

namespace {

// Province_CanExpandBuilding: EDI = country, (bld, prov), RET 8
constexpr uint32_t kCanExpand = 0x00511E70;
// Building_ComputeExpandCost: EDX = prov, ESI = i64 out, (bld, country, stockpile)
constexpr uint32_t kExpandCost = 0x004DA790;
// ConstructBuildingCommand_ctor: stdcall (this, bld, provId, tag, countryIdx), RET 0x14
constexpr uint32_t kConstructCommandCtor = 0x0057B560;
constexpr uint32_t kConstructCommandSize = 0x58;
// CInGameIdler (world+0xB24): vt[0x44] -> +0x38 network session, vt[0x18] NetSession_QueueCommand (command)
constexpr uint32_t kIdlerSessionOwner = 0x44;
constexpr uint32_t kSessionOwnerSession = 0x38;
constexpr uint32_t kSessionQueueCommand = 0x18;
constexpr uint32_t kProvinceId = 0x58;
constexpr uint32_t kCountryStockpile = 0x1184;

template <class Fn>
Fn method(uintptr_t object, uint32_t slot) {
    return read<Fn>(read<uintptr_t>(object) + slot);
}

}  // namespace

bool can_expand(uintptr_t country, uintptr_t building, uintptr_t province) {
    uintptr_t fn = hooks::exe_va(kCanExpand);
    unsigned char result;
    __asm {
        push edi
        mov edi, country
        push province
        push building
        call fn
        pop edi
        mov result, al
    }
    return result != 0;
}

int64_t expand_cost(uintptr_t country, uintptr_t building, uintptr_t province) {
    uintptr_t fn = hooks::exe_va(kExpandCost);
    uintptr_t stockpile = country + kCountryStockpile;
    int64_t cost = 0;
    int64_t* cost_out = &cost;
    __asm {
        push esi
        mov esi, cost_out
        mov edx, province
        push stockpile
        push country
        push building
        call fn
        pop esi
    }
    return cost;
}

void post_construction(uintptr_t building, uintptr_t province, uint32_t tag, int index) {
    uintptr_t idler = read<uintptr_t>(world() + kWorldIdler);
    if (!idler) {
        return;
    }
    // The session first: a command is only built when there is somewhere to queue it (no session owner
    // or session, e.g. outside a running game, means nothing to post).
    using PtrMethod = uintptr_t(__fastcall*)(uintptr_t, void*);
    uintptr_t owner = method<PtrMethod>(idler, kIdlerSessionOwner)(idler, nullptr);
    uintptr_t session = owner ? read<uintptr_t>(owner + kSessionOwnerSession) : 0;
    if (!session) {
        return;
    }
    void* memory = game_new(kConstructCommandSize);
    if (!memory) {
        return;
    }
    using Ctor = uintptr_t(__stdcall*)(void*, uintptr_t, uint32_t, uint32_t, int);
    uintptr_t command = reinterpret_cast<Ctor>(hooks::exe_va(kConstructCommandCtor))(
        memory, building, read<uint32_t>(province + kProvinceId), tag, index);
    using Queue = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    method<Queue>(session, kSessionQueueCommand)(session, nullptr, command);
}

}  // namespace se::game
