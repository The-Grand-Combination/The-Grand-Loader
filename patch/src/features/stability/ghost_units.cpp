#include "features/stability/ghost_units.h"

#include <cstdint>

#include "game/world.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/diplomacy.md, "Units of a dead tag"):
//
//   A country's units, armies and fleets alike, are the list at country+0x7B4 (node {unit +0, prev +4,
//   next +8}, count at +0x7BC); a unit's owner index is unit+0xC4. TransferProvinces (0x004B5A90) moves
//   provinces and never touches the old owner's units. Of its ten callers only Country_Annex (0x00518620)
//   deletes the victim's units, after the transfer, with
//     0x00518E5F  CMP [victim+0x7BC],0 / JE              loop while the victim has units
//     0x00518E70  MOV ECX,[victim+0x7B4]; MOV EDX,[ECX]  the first unit
//     0x00518E78  MOV ECX,[g_world]; PUSH EDX
//     0x00518E7F  CALL World_DeleteArmy (0x0068AA10)     thiscall (ECX = world, unit), RET 4
//   World_DeleteArmy takes a unit out of its battles and its fleet, returns its regiments' pops and
//   removes it from the owner's list. secede_province (SecedeProvinceEffect_Execute 0x00887420) deletes
//   only the old owner's armies without regiments; release, province transfers in peace deals and the
//   conquer-province command delete nothing. A tag that loses its last province that way keeps its units:
//   ghosts in other countries' provinces, with no owner AI (Country_DailyUpdate skips
//   AI_DailyUpdate for a tag without provinces) and no player.
//
//   The CALL DayTick at 0x00683AB1 (Update_World_and_Advance_Time, PUSH world; on every machine, once per
//   game day, outside any walk over countries or units) is chained to a thunk that first deletes, with the
//   same loop, the units of every country from index 1 that owns no province, except REB, which never
//   owns provinces and owns every rebel army. The result differs from vanilla only where vanilla leaves
//   ghosts, and the same way on every machine: all players of a multiplayer game need the same setting.

namespace cp::features {

namespace {

constexpr uint32_t kDayTickCallRva = 0x283AB1;     // CALL DayTick in Update_World_and_Advance_Time
constexpr uint32_t kDayTickRva = 0x2859C0;         // DayTick
constexpr uint32_t kDeleteUnitRva = 0x28AA10;      // World_DeleteArmy
constexpr unsigned char kDeleteUnitBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};  // PUSH EBP; MOV EBP,ESP; AND ESP,-8
constexpr uint32_t kCountryUnits = 0x7B4;          // list head node
constexpr uint32_t kCountryUnitCount = 0x7BC;
constexpr uint32_t kUnitOwner = 0xC4;              // country index
constexpr uint32_t kRebelTag = 'R' | ('E' << 8) | ('B' << 16);
constexpr uint32_t kMaxLogLines = 64;

uintptr_t g_next = 0;         // what the site called before us (DayTick or another hook)
uintptr_t g_delete_unit = 0;
uint32_t g_log_lines = 0;
uint64_t g_deleted = 0;

using game::read32;

// World_DeleteArmy(world, unit): thiscall, RET 4. EBX/ESI/EDI are saved around it as well.
void delete_unit(uintptr_t world, uintptr_t unit) {
    uintptr_t fn = g_delete_unit;
    __asm {
        push ebx
        push esi
        push edi
        mov ecx, world
        push unit
        call fn
        pop edi
        pop esi
        pop ebx
    }
}

// Deletes the units of `country`; returns how many went. Stops when a deletion does not shorten the
// list or the first unit names another owner (World_DeleteArmy would only log and return).
uint32_t clear_units(uintptr_t world, uintptr_t country, uint32_t index) {
    uint32_t deleted = 0;
    for (uint32_t count = read32(country + kCountryUnitCount); count != 0;) {
        uintptr_t node = read32(country + kCountryUnits);
        uintptr_t unit = node ? read32(node) : 0;
        if (!unit || read32(unit + kUnitOwner) != index) {
            break;
        }
        delete_unit(world, unit);
        uint32_t left = read32(country + kCountryUnitCount);
        if (left >= count) {
            break;
        }
        count = left;
        deleted++;
    }
    return deleted;
}

void __stdcall before_day_tick(uintptr_t world) {
    if (!world) {
        return;
    }
    size_t countries = game::country_count();
    for (size_t i = 1; i < countries; i++) {
        uintptr_t country = game::country(i);
        if (!country || read32(country + kCountryUnitCount) == 0 ||
            read32(country + game::kCountryProvincesEnd) != read32(country + game::kCountryProvinces) ||
            (read32(country + game::kCountryTag) & 0xFFFFFF) == kRebelTag) {
            continue;
        }
        uint32_t before = read32(country + kCountryUnitCount);
        uint32_t deleted = clear_units(world, country, static_cast<uint32_t>(i));
        g_deleted += deleted;
        if (g_log_lines < kMaxLogLines) {
            g_log_lines++;
            uint32_t tag = read32(country + game::kCountryTag);
            char date[24];
            game::format_date(game::day(world), date, sizeof(date));
            logf("fix_ghost_units: %s, %c%c%c (index %u) has no provinces: %u of %u units deleted%s", date,
                 tag & 0xFF, (tag >> 8) & 0xFF, (tag >> 16) & 0xFF, static_cast<unsigned>(i), deleted, before,
                 g_log_lines == kMaxLogLines ? " (further deletions are not logged)" : "");
        }
    }
}

// Stack at entry, in place of DayTick: [return to the site][world]. After CP_SAVE_ALL the world is at
// [esp + CP_SAVED_ALL_BYTES + 4]; the restore puts everything back as at entry, and the jump leaves the
// call to DayTick (or the next hook) exactly as the site made it.
__declspec(naked) void day_tick_thunk() {
    CP_SAVE_ALL
    __asm {
        push dword ptr [esp + CP_SAVED_ALL_BYTES + 4]   ; world
        call before_day_tick
    }
    CP_RESTORE_ALL
    __asm jmp dword ptr [g_next]
}

}  // namespace

void install_ghost_units(const Config& config) {
    if (!config.fix_ghost_units) {
        logf("fix_ghost_units: disabled");
        return;
    }
    g_delete_unit = hooks::exe_addr(kDeleteUnitRva);
    if (!hooks::expect_bytes(g_delete_unit, kDeleteUnitBytes, sizeof(kDeleteUnitBytes))) {
        logf("fix_ghost_units: World_DeleteArmy at %08X not recognised, off", hooks::listing_va(kDeleteUnitRva));
        return;
    }
    if (!hooks::chain_call_logged("fix_ghost_units", kDayTickCallRva, kDayTickRva,
                                  reinterpret_cast<uintptr_t>(&day_tick_thunk), g_next)) {
        return;
    }
    logf("fix_ghost_units: active, units of tags without provinces (not REB) are deleted before each day");
}

}  // namespace cp::features
