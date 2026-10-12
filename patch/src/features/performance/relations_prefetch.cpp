#include "features/performance/relations_prefetch.h"

#include <windows.h>
#include <xmmintrin.h>

#include <cstdint>
#include <vector>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/performance.md). Each scan iterates the other countries that have provinces
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

// The countries the scans visit, split once per game day instead of testing every tag's provinces on
// every call (mid-game profile: ~1.3 M such tests a day, most of this feature's own cost). A country that
// gains or loses its provinces during the day is prefetched by yesterday's list until the next day:
// prefetching never changes results, only which lines are warm.
struct CountryLists {
    std::vector<uint32_t> live;  // country index (+0x20) of every tag with provinces
    std::vector<uint32_t> dead;  // index != 0, no provinces
    std::vector<uint32_t> all;   // index of every tag 1..count-1 (the AI target scan's loop)
    int32_t date = -1;
    int32_t count = -1;
};
CountryLists g_lists;
uintptr_t g_lists_world_slot = 0;

const CountryLists* country_lists() {
    uintptr_t manager = read<uint32_t>(g_country_manager_slot);
    uintptr_t world = read<uint32_t>(g_lists_world_slot);
    if (!manager || !world) {
        return nullptr;
    }
    int32_t countries = read<int32_t>(manager);
    int32_t date = read<int32_t>(world + 0xB0C) / 24;
    if (date == g_lists.date && countries == g_lists.count) {
        return &g_lists;
    }
    g_lists.live.clear();
    g_lists.dead.clear();
    g_lists.all.clear();
    uintptr_t array = read<uint32_t>(manager + 4);
    for (int32_t i = 0; i < countries; i++) {
        uintptr_t other = read<uint32_t>(array + i * 4);
        if (!other) {
            continue;
        }
        uint32_t index = read<uint32_t>(other + kCountryIndex);
        bool has_provinces = read<uint32_t>(other + kCountryProvincesEnd) != read<uint32_t>(other + kCountryProvincesBegin);
        if (i >= 1) {
            g_lists.all.push_back(index);
        }
        if (has_provinces) {
            g_lists.live.push_back(index);
        } else if (index != 0) {
            g_lists.dead.push_back(index);
        }
    }
    g_lists.date = date;
    g_lists.count = countries;
    return &g_lists;
}

inline void prefetch_list(uintptr_t relations, const std::vector<uint32_t>& indices, const uint32_t* offsets, int count) {
    for (uint32_t index : indices) {
        uintptr_t rel = read<uint32_t>(relations + index * 4);
        for (int k = 0; k < count; k++) {
            _mm_prefetch(reinterpret_cast<const char*>(rel + offsets[k]), _MM_HINT_T0);
        }
    }
}

// Prefetches, for every other country with provinces, the cache lines of `country`'s relation with it
// that hold the given offsets. Prefetching never faults, so a stale pointer costs nothing.
void prefetch_relations(uintptr_t country, const uint32_t* offsets, int count) {
    const CountryLists* lists = country_lists();
    uintptr_t relations = country ? read<uint32_t>(country + kCountryRelations) : 0;
    if (lists && relations) {
        prefetch_list(relations, lists->live, offsets, count);
    }
}

constexpr uint32_t kRecalcOffsets[] = {0x00, 0x2C, 0x38, 0x70};  // +0x2C: Country_PrePass loop at 0x00508DEB
constexpr uint32_t kMarketOffsets[] = {0x34};
constexpr uint32_t kPurgeOffsets[] = {0x20, 0x50, 0xA8};

// Country_PrePass's loop at 0x00508DA7 reads relation +0x2C for every tag with an index, provinces or not
// (its body is data-dependent, so dead tags cannot be skipped). prefetch_relations only covers countries
// with provinces; this covers the rest for that one field.
void prefetch_dead_relations(uintptr_t country, uint32_t offset) {
    const CountryLists* lists = country_lists();
    uintptr_t relations = country ? read<uint32_t>(country + kCountryRelations) : 0;
    if (lists && relations) {
        prefetch_list(relations, lists->dead, &offset, 1);
    }
}

void __stdcall before_recalc(uintptr_t country) {
    prefetch_relations(country, kRecalcOffsets, 4);
    prefetch_dead_relations(country, 0x2C);
}
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



// FUN_0052FCD0 (stdcall 2, RET 8), called once per country from Country_DailyUpdate (CALL at 0x0050B6B9,
// args: country, 0): for every country of world+0xADC with provinces other than self it reads
// `other+0x1580[self]` (8 bytes): one miss on the other country's array per pair (48 % of its samples at
// 0x00530388). Two passes: the array pointers' lines, then the elements.
constexpr uint32_t kPairCallRva = 0x10B6B9;
constexpr uint32_t kPairRva = 0x12FCD0;
constexpr uint32_t kWorldPtrRva = 0xE588E8;
constexpr uint32_t kWorldLiving = 0xADC;   // vector<CCountry*> walked by FUN_0052FCD0 and FUN_00522CF0
constexpr uint32_t kCountryPairArray = 0x1580;

uintptr_t g_pair = 0;
uintptr_t g_world_slot = 0;

void __stdcall before_pair(uintptr_t country) {
    uintptr_t world = read<uint32_t>(g_world_slot);
    if (!world || !country) {
        return;
    }
    uint32_t self = read<uint32_t>(country + kCountryIndex);
    uintptr_t begin = read<uint32_t>(world + kWorldLiving), end = read<uint32_t>(world + kWorldLiving + 4);
    for (uintptr_t it = begin; it < end; it += 4) {
        _mm_prefetch(reinterpret_cast<const char*>(read<uint32_t>(it) + kCountryPairArray), _MM_HINT_T0);
    }
    for (uintptr_t it = begin; it < end; it += 4) {
        uintptr_t other = read<uint32_t>(it);
        _mm_prefetch(reinterpret_cast<const char*>(read<uint32_t>(other + kCountryPairArray) + self * 8), _MM_HINT_T0);
    }
}

__declspec(naked) void pair_thunk() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 40]
        call before_pair
        popfd
        popad
        jmp dword ptr [g_pair]
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
    const CountryLists* lists = country_lists();
    if (!manager || !ai || !lists) {
        return;
    }
    int countries = read<int32_t>(manager);
    int self_index = read<int32_t>(ai + kAiCountryIndex);
    if (self_index <= 0 || self_index >= countries) {
        return;
    }
    uintptr_t self = read<uint32_t>(read<uint32_t>(manager + 4) + self_index * 4);
    uintptr_t relations = self ? read<uint32_t>(self + kCountryRelations) : 0;
    if (relations) {
        constexpr uint32_t kOffset = 0x34;
        prefetch_list(relations, lists->all, &kOffset, 1);
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
    g_lists_world_slot = hooks::exe_addr(0xE588E8);  // g_world
    g_recalc = hooks::exe_addr(kRecalcRva);
    g_market = hooks::exe_addr(kMarketRva);
    g_purge = hooks::exe_addr(kPurgeRva);
    g_pair = hooks::exe_addr(kPairRva);
    g_world_slot = hooks::exe_addr(kWorldPtrRva);
    struct Site {
        uint32_t call_rva;
        uintptr_t target;
        void (*thunk)();
        const char* name;
    } sites[] = {
        {kRecalcCallRva, g_recalc, &recalc_thunk, "Country_Recalculate"},
        {kMarketCallRva, g_market, &market_thunk, "market capacity (FUN_00522CF0)"},
        {kPurgeCallRva, g_purge, &purge_thunk, "CountryRelation_DailyPurge"},
        {kPairCallRva, g_pair, &pair_thunk, "country pair array (FUN_0052FCD0)"},
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
    logf("prefetch_relations: active on %d of 4 country-pair scans", active);
}


void install_ai_relations_prefetch(const Config& config) {
    if (!config.prefetch_ai_relations) {
        logf("prefetch_ai_relations: disabled");
        return;
    }
    g_country_manager_slot = hooks::exe_addr(kCountryManagerPtrRva);
    g_lists_world_slot = hooks::exe_addr(0xE588E8);  // g_world
    g_ai_scan = hooks::exe_addr(kAiScanRva);
    if (!hooks::replace_pointer(hooks::exe_addr(kAiScanSlotRva), g_ai_scan, reinterpret_cast<uintptr_t>(&ai_scan_thunk))) {
        logf("prefetch_ai_relations: vtable slot %08X not recognised or already hooked, feature off", kAiScanSlotRva + 0x400000);
        return;
    }
    logf("prefetch_ai_relations: active, relations prefetched before each AI country's daily target scan");
}

}  // namespace cp::features
