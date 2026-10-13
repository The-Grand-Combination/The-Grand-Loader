#include "features/multiplayer/state_checksum.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/multiplayer.md):
//
//   Update_World_and_Advance_Time (0x00682EC0), on every machine, per hourly tick command:
//     compare world+0xB74 (vector<int>) with the vector the host put in the command  (CALL Vector_Equals at 0x00683036)
//     clear world+0xB74, push g_rngCalls, push the seed
//     PUSH ESI (world); CALL DayTick (0x006859C0)                                     at 0x00683AB1
//     monthly / yearly work, ...
//   ProcessGameTimeAdvance (0x00682BD0), host only: the next CHourlyTickCommand copies world+0xB74.
//
// So whatever is in the vector after DayTick travels with the next tick and is compared on every client
// against the client's own vector at the same point of its own day. The game's vector is a draw counter
// only. This feature redirects the CALL DayTick to a thunk that runs DayTick and then appends hashes of
// the state (the global RNG position after the day, provinces, countries, pops, the date). The game's
// comparison is limited to its own two elements by sync_check.cpp; ours are compared and logged there.
//
// The vector is a MSVC std::vector<int>: the game clears it with end = begin (capacity stays), and grows
// it with its own operator new / delete. Our push_back does the same with the same functions.

namespace cp::features {

namespace {

constexpr uint32_t kDayTickCallRva = 0x283AB1;  // CALL DayTick in Update_World_and_Advance_Time
constexpr uint32_t kDayTickRva = 0x2859C0;
constexpr size_t kMinCapacity = 16;
constexpr size_t kMaxDepth = 8;

using AllocFn = void*(__cdecl*)(size_t);
using FreeFn = void(__cdecl*)(void*);
AllocFn g_game_new = nullptr;
FreeFn g_game_delete = nullptr;

uintptr_t g_original_day_tick = 0;
uintptr_t g_saved_return[kMaxDepth];
uint32_t g_depth = 0;
bool g_logged_first = false;

const char* const kNames[] = {"rng_calls", "rng_seed", "rng_calls_end", "provinces", "countries", "pops", "date"};
constexpr uint32_t kRngCallsRva = 0xB20C14;  // g_rngCalls

inline uint32_t fnv(uint32_t h, uint32_t v) {
    // FNV-1a over the four bytes of v
    for (int i = 0; i < 4; i++) {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 16777619u;
    }
    return h;
}

uint32_t hash_provinces(uintptr_t world) {
    uint32_t h = 2166136261u;
    uintptr_t it = game::read32(world + game::kWorldProvinces);
    uintptr_t end = game::read32(world + game::kWorldProvincesEnd);
    for (; it < end; it += 4) {
        uintptr_t p = game::read32(it);
        if (!p) {
            h = fnv(h, 0xFFFFFFFFu);
            continue;
        }
        h = fnv(h, game::read32(p + game::kProvinceOwner));
        h = fnv(h, game::read32(p + game::kProvinceController));
    }
    return h;
}

uint32_t hash_countries() {
    uint32_t h = 2166136261u;
    uintptr_t cm = game::country_manager();
    if (!cm) {
        return h;
    }
    uint32_t count = game::read32(cm);
    uintptr_t arr = game::read32(cm + 4);
    for (uint32_t i = 0; i < count; i++) {
        uintptr_t c = game::read32(arr + 4 * i);
        if (!c) {
            continue;
        }
        h = fnv(h, i);
        h = fnv(h, game::read32(c + game::kCountryMoney));
        h = fnv(h, game::read32(c + game::kCountryMoney + 4));
        h = fnv(h, (game::read32(c + game::kCountryProvincesEnd) - game::read32(c + game::kCountryProvinces)) / 4);
    }
    return h;
}

uint32_t hash_pops(uintptr_t world) {
    uint32_t h = 2166136261u;
    uintptr_t it = game::read32(world + game::kWorldProvinces);
    uintptr_t end = game::read32(world + game::kWorldProvincesEnd);
    for (; it < end; it += 4) {
        uintptr_t p = game::read32(it);
        if (!p) {
            continue;
        }
        uintptr_t list = game::read32(p + game::kProvincePopLists);
        uintptr_t list_end = game::read32(p + game::kProvincePopListsEnd);
        for (; list < list_end; list += 16) {
            for (uintptr_t pop = game::read32(list); pop; pop = game::read32(pop + game::kPopNext)) {
                h = fnv(h, game::read32(pop + game::kPopId));
                h = fnv(h, game::read32(pop + game::kPopSize));
            }
        }
    }
    return h;
}

void push_back(uintptr_t vec, uint32_t value) {
    uint32_t* begin = reinterpret_cast<uint32_t*>(game::read32(vec));
    uint32_t* end = reinterpret_cast<uint32_t*>(game::read32(vec + 4));
    uint32_t* cap = reinterpret_cast<uint32_t*>(game::read32(vec + 8));
    if (end == cap) {
        size_t size = static_cast<size_t>(end - begin);
        size_t capacity = static_cast<size_t>(cap - begin);
        size_t wanted = capacity * 2 < kMinCapacity ? kMinCapacity : capacity * 2;
        uint32_t* fresh = static_cast<uint32_t*>(g_game_new(wanted * sizeof(uint32_t)));
        if (size) {
            std::memcpy(fresh, begin, size * sizeof(uint32_t));
        }
        if (begin) {
            g_game_delete(begin);
        }
        begin = fresh;
        end = fresh + size;
        cap = fresh + wanted;
        *reinterpret_cast<uint32_t**>(vec) = begin;
        *reinterpret_cast<uint32_t**>(vec + 8) = cap;
    }
    *end++ = value;
    *reinterpret_cast<uint32_t**>(vec + 4) = end;
}

void after_day_tick() {
    uintptr_t world = game::world();
    if (!world) {
        return;
    }
    uintptr_t vec = world + game::kWorldSyncVector;
    uint32_t values[5] = {
        game::read32(hooks::exe_addr(kRngCallsRva)),  // the global RNG position after this day: the host's position when a client compares
        hash_provinces(world),
        hash_countries(),
        hash_pops(world),
        game::read32(world + game::kWorldDate),
    };
    for (uint32_t v : values) {
        push_back(vec, v);
    }
    if (!g_logged_first) {
        g_logged_first = true;
        size_t n = (game::read32(vec + 4) - game::read32(vec)) / 4;
        logf("state_checksum: first day hashed, the sync vector now has %u elements", static_cast<unsigned>(n));
    }
}

// Replaces the CALL DayTick. The site pushes the world and DayTick pops it (RET 4), so the return
// address is swapped for `after` and saved on a small stack of ours; `after` runs the hashing with
// every register preserved and returns to the site.
__declspec(naked) void day_tick_thunk() {
    __asm {
        // stack: [return to site][world]
        push eax
        push ecx
        mov eax, [esp + 8]                 // return address
        mov ecx, g_depth
        mov [g_saved_return + ecx * 4], eax
        inc ecx
        mov g_depth, ecx
        pop ecx
        pop eax
        mov dword ptr [esp], offset after  // DayTick returns here
        jmp dword ptr [g_original_day_tick]
    after:
        // DayTick's RET 4 popped the world; the site's return address is on our stack
        pushad
        pushfd
        call after_day_tick
        popfd
        popad
        push eax
        push ecx
        mov ecx, g_depth
        dec ecx
        mov g_depth, ecx
        mov eax, [g_saved_return + ecx * 4]
        pop ecx
        xchg eax, [esp]                    // restore eax, leave the site's return address on top
        ret
    }
}

}  // namespace

const char* sync_element_name(size_t index) {
    return index < sizeof(kNames) / sizeof(kNames[0]) ? kNames[index] : "extra";
}

size_t sync_element_index(const char* name) {
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (std::strcmp(kNames[i], name) == 0) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

void install_state_checksum(const Config& config) {
    if (!config.state_checksum) {
        logf("state_checksum: disabled");
        return;
    }
    g_game_new = reinterpret_cast<AllocFn>(hooks::exe_addr(game::kOperatorNewRva));
    g_game_delete = reinterpret_cast<FreeFn>(hooks::exe_addr(game::kOperatorDeleteRva));
    uintptr_t site = hooks::exe_addr(kDayTickCallRva);
    if (*reinterpret_cast<const unsigned char*>(site) != 0xE8) {
        logf("state_checksum: no CALL at %08X, feature off", kDayTickCallRva + 0x400000);
        return;
    }
    // Chain-friendly: whatever the site calls now (DayTick, or another plugin's hook of it) is what we call.
    int32_t rel;
    std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
    uintptr_t current = site + 5 + static_cast<uintptr_t>(rel);
    if (current != hooks::exe_addr(kDayTickRva)) {
        logf("state_checksum: the DayTick call site is already redirected (to %08X); chaining to it",
             static_cast<unsigned>(current - hooks::exe_base() + 0x400000));
    }
    g_original_day_tick = current;
    if (!hooks::redirect_call(site, current, reinterpret_cast<uintptr_t>(&day_tick_thunk))) {
        logf("state_checksum: could not patch the DayTick call site, feature off");
        return;
    }
    logf("state_checksum: active, 5 state hashes appended to the daily sync vector");
}

}  // namespace cp::features
