#include "features/performance/pops_prefetch.h"

#include <windows.h>
#include <xmmintrin.h>

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/economy.md, docs/re/systems/performance.md):
//
//   WorldMarketUpdate (0x00484060) calls Market_UpdateCountryPops (0x00485E40) once per country, CALL at
//   0x0048468F with four stack arguments: market (EDI), country (EBX), ...
//   Market_UpdateCountryPops: for each province id in country+0x9D8..+0x9DC, province = world+0xACC[id];
//     for each pop type id in market+0x804..+0x808, walk the linked list headed by
//     *(province+0x194 + type*16), next = pop+0x27C. Per pop it reads fields all over the object
//     (+0x58 .. +0x27C) and the object at pop+0x1D4 (+0xB0, +0xE8).
//   The samples sit right after the first read of each pop (0x004853D7, 0x004869F6, 0x00486A1C): one
//   cache miss after another, because the next pop is only known once the current one has arrived.
//
// Before the call, the thunk walks all the country's lists at once (one cursor per list, advanced
// together) and prefetches every pop's lines, so the misses overlap. Nothing is written.

namespace cp::features {

namespace {

constexpr uint32_t kCallSiteRva = 0x08468F;       // CALL Market_UpdateCountryPops in WorldMarketUpdate
constexpr uint32_t kUpdatePopsRva = 0x085E40;     // Market_UpdateCountryPops
constexpr uint32_t kWorldPtrRva = 0xE588E8;       // g_world
constexpr uint32_t kWorldProvinces = 0xACC;
constexpr uint32_t kMarketPopTypesBegin = 0x804;
constexpr uint32_t kMarketPopTypesEnd = 0x808;
constexpr uint32_t kCountryProvincesBegin = 0x9D8;
constexpr uint32_t kCountryProvincesEnd = 0x9DC;
constexpr uint32_t kProvincePopLists = 0x194;     // 16 bytes per pop type, +0 = first pop
constexpr uint32_t kPopNext = 0x27C;
constexpr uint32_t kPopSize = 0x280;               // at least: next is the last field read
constexpr uint32_t kPopLinked = 0x1D4;             // object read at +0xB0 and +0xE8
constexpr int kMaxCursors = 4096;

uintptr_t g_update_pops = 0;
uintptr_t g_world_slot = 0;
uintptr_t g_cursors[kMaxCursors];

template <typename T>
inline T read(uintptr_t at) {
    return *reinterpret_cast<const T*>(at);
}

// Advances every cursor one pop at a time until all lists end. The prefetches of a round do not depend on
// each other and neither do the `next` loads, so the CPU keeps many misses in flight.
void walk(int count) {
    while (count > 0) {
        for (int i = 0; i < count; i++) {
            const char* pop = reinterpret_cast<const char*>(g_cursors[i]);
            for (uint32_t off = 0; off < kPopSize; off += 64) {
                _mm_prefetch(pop + off, _MM_HINT_T0);
            }
        }
        int live = 0;
        for (int i = 0; i < count; i++) {
            uintptr_t pop = g_cursors[i];
            uintptr_t linked = read<uint32_t>(pop + kPopLinked);
            _mm_prefetch(reinterpret_cast<const char*>(linked + 0xB0), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(linked + 0xE8), _MM_HINT_T0);
            uintptr_t next = read<uint32_t>(pop + kPopNext);
            if (next) {
                g_cursors[live++] = next;
            }
        }
        count = live;
    }
}

void __stdcall before_update_pops(uintptr_t market, uintptr_t country) {
    uintptr_t world = read<uint32_t>(g_world_slot);
    if (!world || !market || !country) {
        return;
    }
    uintptr_t provinces = read<uint32_t>(world + kWorldProvinces);
    const int32_t* types = reinterpret_cast<const int32_t*>(read<uint32_t>(market + kMarketPopTypesBegin));
    const int32_t* types_end = reinterpret_cast<const int32_t*>(read<uint32_t>(market + kMarketPopTypesEnd));
    const int32_t* ids = reinterpret_cast<const int32_t*>(read<uint32_t>(country + kCountryProvincesBegin));
    const int32_t* ids_end = reinterpret_cast<const int32_t*>(read<uint32_t>(country + kCountryProvincesEnd));
    if (!provinces || types >= types_end) {
        return;
    }
    int count = 0;
    for (; ids < ids_end; ids++) {
        uintptr_t province = read<uint32_t>(provinces + *ids * 4);
        uintptr_t lists = province ? read<uint32_t>(province + kProvincePopLists) : 0;
        if (!lists) {
            continue;
        }
        for (const int32_t* t = types; t < types_end; t++) {
            uintptr_t head = read<uint32_t>(lists + *t * 16);
            if (head) {
                g_cursors[count++] = head;
                if (count == kMaxCursors) {
                    walk(count);
                    count = 0;
                }
            }
        }
    }
    walk(count);
}

// Stack at entry: [ret][market][country][...]. After pushad (32) + pushfd (4), market is at [esp+40]
// and country at [esp+44]; the first push moves both down by 4.
__declspec(naked) void update_pops_thunk() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 44]       ; country
        push dword ptr [esp + 44]       ; market
        call before_update_pops
        popfd
        popad
        jmp dword ptr [g_update_pops]
    }
}


// DayTick (0x006859C0), province loop: for each province (EBX = world, EDI = index, ESI = province),
// CALL FUN_004B9C60 at 0x00685B09, which walks every pop list of ESI (+0x194..+0x198, 16 bytes per type)
// and calls FUN_0095EBF0 per pop; its samples sit after `MOV EAX,[EAX+0x27C]` (0x004B9C9B). Every 8th
// province, the lists of the next 8 provinces are walked together (a few dozen pops, cache-sized).
constexpr uint32_t kProvinceCallRva = 0x285B09;   // CALL FUN_004B9C60 in DayTick
constexpr uint32_t kProvincePopsRva = 0x0B9C60;   // FUN_004B9C60
constexpr uint32_t kWorldProvincesEnd = 0xAD0;
constexpr uint32_t kProvincePopListsEnd = 0x198;
constexpr int kProvinceBatch = 8;

uintptr_t g_province_pops = 0;

void __stdcall before_province_pops(uintptr_t world, int index) {
    if (!world || index % kProvinceBatch != 0) {
        return;
    }
    uintptr_t begin = read<uint32_t>(world + kWorldProvinces);
    int provinces = static_cast<int>((read<uint32_t>(world + kWorldProvincesEnd) - begin) / 4);
    int end = index + kProvinceBatch < provinces ? index + kProvinceBatch : provinces;
    int count = 0;
    for (int p = index; p < end; p++) {
        uintptr_t province = read<uint32_t>(begin + p * 4);
        if (!province) {
            continue;
        }
        uintptr_t lists = read<uint32_t>(province + kProvincePopLists);
        uintptr_t lists_end = read<uint32_t>(province + kProvincePopListsEnd);
        for (uintptr_t at = lists; at + 16 <= lists_end; at += 16) {
            uintptr_t head = read<uint32_t>(at);
            if (head) {
                g_cursors[count++] = head;
                if (count == kMaxCursors) {
                    walk(count);
                    count = 0;
                }
            }
        }
    }
    walk(count);
}

// The callee takes the province in ESI; every register is kept, so it still finds it there.
__declspec(naked) void province_pops_thunk() {
    __asm {
        pushad
        pushfd
        push edi                        ; index
        push ebx                        ; world
        call before_province_pops
        popfd
        popad
        jmp dword ptr [g_province_pops]
    }
}


// ProcessMarketAndGoodsDistribution (0x004808D0) has the same province x pop-type list walk inline, with no
// call per country to hang a batch on; per pop it does `MOV EAX,[EDI+0x27C]` (next), `MOV ESI,EDI` and
// CALL FUN_0095EB00 at 0x00481860 (pop in ESI), whose samples sit after the pop's first read (90 % at
// 0x0095EB63). Here the next pop (EAX) is prefetched while the current one is processed.
constexpr uint32_t kDistributionCallRva = 0x081860;  // CALL FUN_0095EB00
constexpr uint32_t kPopDistributeRva = 0x55EB00;     // FUN_0095EB00

uintptr_t g_pop_distribute = 0;

void __stdcall prefetch_pop(uintptr_t pop) {
    if (!pop) {
        return;
    }
    for (uint32_t off = 0; off < kPopSize; off += 64) {
        _mm_prefetch(reinterpret_cast<const char*>(pop + off), _MM_HINT_T0);
    }
}

__declspec(naked) void distribution_thunk() {
    __asm {
        pushad
        pushfd
        push eax                        ; next pop
        call prefetch_pop
        popfd
        popad
        jmp dword ptr [g_pop_distribute]
    }
}


// Every pop of a country: all lists of all its provinces (country+0x9D8 ids, every type entry of
// province+0x194..+0x198), walked together. Used before Country_DailyUpdate (its monthly pop pass reads
// every pop daily to test pop.id % month_length == day) and, through prefetch_country_pops, at the start
// of Country_PrePass (FUN_0050B9B0 walks the same lists, stalling after [pop+0x27C] at 0x0050C089).
void walk_country_pops(uintptr_t country) {
    uintptr_t world = read<uint32_t>(g_world_slot);
    if (!world || !country) {
        return;
    }
    uintptr_t provinces = read<uint32_t>(world + kWorldProvinces);
    const int32_t* ids = reinterpret_cast<const int32_t*>(read<uint32_t>(country + kCountryProvincesBegin));
    const int32_t* ids_end = reinterpret_cast<const int32_t*>(read<uint32_t>(country + kCountryProvincesEnd));
    if (!provinces) {
        return;
    }
    int count = 0;
    for (; ids < ids_end; ids++) {
        uintptr_t province = read<uint32_t>(provinces + *ids * 4);
        if (!province) {
            continue;
        }
        uintptr_t lists = read<uint32_t>(province + kProvincePopLists);
        uintptr_t lists_end = read<uint32_t>(province + kProvincePopListsEnd);
        for (uintptr_t at = lists; at + 16 <= lists_end; at += 16) {
            uintptr_t head = read<uint32_t>(at);
            if (head) {
                g_cursors[count++] = head;
                if (count == kMaxCursors) {
                    walk(count);
                    count = 0;
                }
            }
        }
    }
    walk(count);
}

// Country_DailyUpdate (0x005091A0, stdcall 2: country, random array), CALL at 0x00685ED1 in DayTick.
constexpr uint32_t kDailyUpdateCallRva = 0x285ED1;
constexpr uint32_t kDailyUpdateRva = 0x1091A0;
uintptr_t g_daily_update = 0;
bool g_enabled = false;

void __stdcall before_daily_update(uintptr_t country) { walk_country_pops(country); }

__declspec(naked) void daily_update_thunk() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 40]       ; country
        call before_daily_update
        popfd
        popad
        jmp dword ptr [g_daily_update]
    }
}

}  // namespace

void install_pops_prefetch(const Config& config) {
    if (!config.prefetch_pops) {
        logf("prefetch_pops: disabled");
        return;
    }
    g_world_slot = hooks::exe_addr(kWorldPtrRva);
    g_update_pops = hooks::exe_addr(kUpdatePopsRva);
    g_province_pops = hooks::exe_addr(kProvincePopsRva);
    g_pop_distribute = hooks::exe_addr(kPopDistributeRva);
    g_daily_update = hooks::exe_addr(kDailyUpdateRva);
    struct Site {
        uint32_t call_rva;
        uintptr_t target;
        void (*thunk)();
        const char* name;
    } sites[] = {
        {kCallSiteRva, g_update_pops, &update_pops_thunk, "market"},
        {kProvinceCallRva, g_province_pops, &province_pops_thunk, "DayTick provinces"},
        {kDistributionCallRva, g_pop_distribute, &distribution_thunk, "market distribution"},
        {kDailyUpdateCallRva, g_daily_update, &daily_update_thunk, "Country_DailyUpdate"},
    };
    int active = 0;
    for (const Site& s : sites) {
        if (hooks::redirect_call(hooks::exe_addr(s.call_rva), s.target, reinterpret_cast<uintptr_t>(s.thunk))) {
            active++;
        } else {
            logf("prefetch_pops: call site %08X (%s) not recognised or already hooked, skipped", s.call_rva + 0x400000, s.name);
        }
    }
    g_enabled = true;
    logf("prefetch_pops: active on %d of 4 pop loops (country market update, DayTick province pass, market distribution, Country_DailyUpdate)", active);
}

void prefetch_country_pops(uintptr_t country) {
    if (g_enabled) {
        walk_country_pops(country);
    }
}

}  // namespace cp::features
