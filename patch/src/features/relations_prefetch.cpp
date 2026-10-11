#include "features/relations_prefetch.h"

#include <windows.h>
#include <xmmintrin.h>

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (re/systems/performance.md). Each scan iterates the other countries that have provinces
// and reads `self+0xBE8[other]`, the relation object of the pair; the samples sit right after the first
// read of that object:
//
//   Country_Recalculate (0x00515490, stdcall 1)      from Country_PrePass, CALL at 0x005089DB
//       reads rel +0x08 +0x0C +0x20 +0x24 +0x28 +0x34 +0x38 +0x70; stall after CMP [rel+0x20] (0x00515FCB)
//   FUN_00522CF0 (stdcall 2, market capacity)          from Market_AddRealDemand, CALL at 0x00487483
//       reads rel +0x34; stall after CMP [rel+0x34] (0x00522E23)
//   CountryRelation_DailyPurge (0x0092E290, stdcall 1) from Country_DailyUpdate's pair loop, CALL at 0x0050AC44
//       top level reads rel +0x50 +0xA8 +0x2C +0x30 +0x28 +0x20 only; every other access is inside a
//       branch taken when one of them is non-zero, so with all six zero the call does nothing
//
// The thunks keep every register and the flags (the game is built with whole-program optimisation and
// its callers may rely on more than the documented convention), do their work, and then jump to the
// original function with the stack exactly as the caller left it.

namespace cp::features {

namespace {

constexpr uint32_t kCountryManagerPtrRva = 0xE587E4;  // DAT_012587E4: +0 count, +4 CCountry* array
constexpr uint32_t kCountryIndex = 0x20;
constexpr uint32_t kCountryProvincesBegin = 0x9D8;
constexpr uint32_t kCountryProvincesEnd = 0x9DC;
constexpr uint32_t kCountryRelations = 0xBE8;

constexpr uint32_t kRecalcCallRva = 0x1089DB;
constexpr uint32_t kRecalcRva = 0x115490;
constexpr uint32_t kMarketCallRva = 0x087483;
constexpr uint32_t kMarketRva = 0x122CF0;
constexpr uint32_t kPurgeCallRva = 0x10AC44;
constexpr uint32_t kPurgeRva = 0x52E290;

uintptr_t g_recalc = 0;
uintptr_t g_market = 0;
uintptr_t g_purge = 0;
uintptr_t g_country_manager_slot = 0;
uintptr_t g_purge_self = 0;  // country whose relations the purge loop last prefetched
uint8_t g_skip_purge = 0;

template <typename T>
inline T read(uintptr_t at) {
    return *reinterpret_cast<const T*>(at);
}

// Prefetches, for every other country with provinces, the cache lines of `country`'s relation with it
// that hold the given offsets. Prefetching never faults, so a stale pointer costs nothing.
void prefetch_relations(uintptr_t country, const uint32_t* offsets, int count) {
    uintptr_t manager = read<uint32_t>(g_country_manager_slot);
    uintptr_t relations = country ? read<uint32_t>(country + kCountryRelations) : 0;
    if (!manager || !relations) {
        return;
    }
    int countries = read<int32_t>(manager);
    uintptr_t array = read<uint32_t>(manager + 4);
    for (int i = 0; i < countries; i++) {
        uintptr_t other = read<uint32_t>(array + i * 4);
        if (!other || read<uint32_t>(other + kCountryProvincesEnd) == read<uint32_t>(other + kCountryProvincesBegin)) {
            continue;
        }
        uintptr_t rel = read<uint32_t>(relations + read<uint32_t>(other + kCountryIndex) * 4);
        for (int k = 0; k < count; k++) {
            _mm_prefetch(reinterpret_cast<const char*>(rel + offsets[k]), _MM_HINT_T0);
        }
    }
}

constexpr uint32_t kRecalcOffsets[] = {0x00, 0x2C, 0x38, 0x70};  // +0x2C: Country_PrePass loop at 0x00508DEB
constexpr uint32_t kMarketOffsets[] = {0x34};
constexpr uint32_t kPurgeOffsets[] = {0x20, 0x50, 0xA8};

void __stdcall before_recalc(uintptr_t country) { prefetch_relations(country, kRecalcOffsets, 4); }
void __stdcall before_market(uintptr_t country) { prefetch_relations(country, kMarketOffsets, 1); }

// Returns 1 when the purge of `rel` would do nothing.
uint8_t __stdcall before_purge(uintptr_t rel, uintptr_t self) {
    if (self != g_purge_self) {
        g_purge_self = self;
        prefetch_relations(self, kPurgeOffsets, 3);
    }
    return read<uint32_t>(rel + 0x50) == 0 && read<uint32_t>(rel + 0xA8) == 0 && read<uint32_t>(rel + 0x2C) == 0 &&
           read<uint32_t>(rel + 0x30) == 0 && read<uint32_t>(rel + 0x28) == 0 && read<uint32_t>(rel + 0x20) == 0;
}

// Stack at entry: [ret][arg1][arg2]. After pushad (32) + pushfd (4), arg1 is at [esp+40].
__declspec(naked) void recalc_thunk() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 40]
        call before_recalc
        popfd
        popad
        jmp dword ptr [g_recalc]
    }
}

__declspec(naked) void market_thunk() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 40]
        call before_market
        popfd
        popad
        jmp dword ptr [g_market]
    }
}

// In Country_DailyUpdate's pair loop EBX is the country being updated (`MOV EAX,[EBX+0xBE8]`).
__declspec(naked) void purge_thunk() {
    __asm {
        pushad
        pushfd
        push ebx
        push dword ptr [esp + 44]       ; rel (arg1 moved by the push above)
        call before_purge
        mov byte ptr [g_skip_purge], al
        popfd
        popad
        cmp byte ptr [g_skip_purge], 0
        jne skip
        jmp dword ptr [g_purge]
    skip:
        ret 4                           ; stdcall, as the original
    }
}


// AI target scan (2026-10-11): FUN_0084B0F0 (thiscall, AI object in ECX, its country index at +0x38) is
// reached only through vtable slot 0x00E2773C, once per AI country per day from the AI job queue. It loops
// over every country (index 1..count, provinces or not) and tests `CMP [self+0xBE8[other] + 0x34], 0`;
// 90 % of its samples sit right after that read (0x0084B29D). Later, rarely, it reads +0x3C.
constexpr uint32_t kAiScanSlotRva = 0xA2773C;
constexpr uint32_t kAiScanRva = 0x44B0F0;
constexpr uint32_t kAiCountryIndex = 0x38;

uintptr_t g_ai_scan = 0;

void __stdcall before_ai_scan(uintptr_t ai) {
    uintptr_t manager = read<uint32_t>(g_country_manager_slot);
    if (!manager || !ai) {
        return;
    }
    int countries = read<int32_t>(manager);
    uintptr_t array = read<uint32_t>(manager + 4);
    int self_index = read<int32_t>(ai + kAiCountryIndex);
    if (self_index <= 0 || self_index >= countries) {
        return;
    }
    uintptr_t self = read<uint32_t>(array + self_index * 4);
    uintptr_t relations = self ? read<uint32_t>(self + kCountryRelations) : 0;
    if (!relations) {
        return;
    }
    for (int i = 1; i < countries; i++) {
        uintptr_t other = read<uint32_t>(array + i * 4);
        uintptr_t rel = read<uint32_t>(relations + read<uint32_t>(other + kCountryIndex) * 4);
        _mm_prefetch(reinterpret_cast<const char*>(rel + 0x34), _MM_HINT_T0);
    }
}

__declspec(naked) void ai_scan_thunk() {
    __asm {
        pushad
        pushfd
        push ecx
        call before_ai_scan
        popfd
        popad
        jmp dword ptr [g_ai_scan]
    }
}

}  // namespace

void install_relations_prefetch(const Config& config) {
    if (!config.prefetch_relations) {
        logf("prefetch_relations: disabled");
        return;
    }
    g_country_manager_slot = hooks::exe_addr(kCountryManagerPtrRva);
    g_recalc = hooks::exe_addr(kRecalcRva);
    g_market = hooks::exe_addr(kMarketRva);
    g_purge = hooks::exe_addr(kPurgeRva);
    struct Site {
        uint32_t call_rva;
        uintptr_t target;
        void (*thunk)();
        const char* name;
    } sites[] = {
        {kRecalcCallRva, g_recalc, &recalc_thunk, "Country_Recalculate"},
        {kMarketCallRva, g_market, &market_thunk, "market capacity (FUN_00522CF0)"},
        {kPurgeCallRva, g_purge, &purge_thunk, "CountryRelation_DailyPurge"},
    };
    int active = 0;
    for (const Site& s : sites) {
        if (hooks::redirect_call(hooks::exe_addr(s.call_rva), s.target, reinterpret_cast<uintptr_t>(s.thunk))) {
            active++;
        } else {
            logf("prefetch_relations: call site %08X (%s) not recognised or already hooked, skipped",
                 s.call_rva + 0x400000, s.name);
        }
    }
    logf("prefetch_relations: active on %d of 3 relation scans", active);
}


void install_ai_relations_prefetch(const Config& config) {
    if (!config.prefetch_ai_relations) {
        logf("prefetch_ai_relations: disabled");
        return;
    }
    g_country_manager_slot = hooks::exe_addr(kCountryManagerPtrRva);
    g_ai_scan = hooks::exe_addr(kAiScanRva);
    if (!hooks::replace_pointer(hooks::exe_addr(kAiScanSlotRva), g_ai_scan, reinterpret_cast<uintptr_t>(&ai_scan_thunk))) {
        logf("prefetch_ai_relations: vtable slot %08X not recognised or already hooked, feature off", kAiScanSlotRva + 0x400000);
        return;
    }
    logf("prefetch_ai_relations: active, relations prefetched before each AI country's daily target scan");
}

}  // namespace cp::features
