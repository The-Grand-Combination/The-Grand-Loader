#pragma once

#include <cstdint>

#include "hooks/memory.h"

// v2game.exe 3.04 layout shared by the features (docs/re/systems/multiplayer.md, tick.md, events.md).
// RVAs are VA - 0x00400000.

namespace cp::game {

constexpr uint32_t kWorldPtrRva = 0xE588E8;           // DAT_012588E8: CWorld*
constexpr uint32_t kCountryManagerPtrRva = 0xE587E4;  // DAT_012587E4: +0 count, +4 CCountry* array
constexpr uint32_t kMtStateRva = 0xB0ECF0;            // g_mtState: the global MT19937, index at +0x9C0
constexpr uint32_t kRngCallsRva = 0xB20C14;           // g_rngCalls: draws on it since the seed
constexpr uint32_t kRngNextRva = 0x5B7610;            // Rng_Next: MT19937 step on the state in EAX
constexpr uint32_t kOperatorNewRva = 0x6AE9AF;        // cdecl (size), malloc or throw
constexpr uint32_t kOperatorDeleteRva = 0x6AE91B;     // cdecl (ptr), free
constexpr uint32_t kInGameIdlerVtableRva = 0xA0A34C;  // CInGameIdler::vftable

// CWorld
constexpr uint32_t kWorldPlayers = 0x18;       // list head: entries +0x1C id (-1 unused), +0x34 date, +0x3C next
constexpr uint32_t kWorldHumanPlayers = 0xAEC;  // vector<u32> per country index: 0 = AI-controlled
constexpr uint32_t kWorldHumanPlayersEnd = 0xAF0;
constexpr uint32_t kWorldProvinces = 0xACC;    // vector<CProvince*> begin
constexpr uint32_t kWorldProvincesEnd = 0xAD0;
constexpr uint32_t kWorldDate = 0xB0C;         // hours
constexpr uint32_t kWorldIdler = 0xB24;        // the active idler; CInGameIdler in a game
constexpr uint32_t kWorldOosShown = 0xBA8;     // u8: the OOS dialog and pause happened; never reset by the game
constexpr uint32_t kWorldWars = 0xB3C;         // list of ongoing wars: node {war +0, ?, next +8}
constexpr uint32_t kWorldSyncVector = 0xB74;   // vector<int> {begin +0, end +4, capacity +8}: the sync check

// CWar: two participant vectors of 8-byte entries, country index at entry+4
constexpr uint32_t kWarAttackers = 0x2C;  // begin; end at +0x30
constexpr uint32_t kWarDefenders = 0x3C;  // begin; end at +0x40

// CInGameIdler
constexpr uint32_t kIdlerSessionManagerSlot = 0x44;  // vt[17]() -> session manager

// Session manager
constexpr uint32_t kSessionManagerIsServer = 0x34;    // 1 in single player and for the host
constexpr uint32_t kSessionManagerExecuting = 0x234;  // 1 while a command (the hourly tick included) runs

// CProvince
constexpr uint32_t kProvinceOwner = 0x12C;       // country index
constexpr uint32_t kProvinceController = 0x134;  // country index
constexpr uint32_t kProvincePopLists = 0x194;    // vector of 16-byte entries (one per pop type): +0 first pop
constexpr uint32_t kProvincePopListsEnd = 0x198;

// CCountry
constexpr uint32_t kCountryTag = 0x1C;
constexpr uint32_t kCountryIndex = 0x20;
constexpr uint32_t kCountryProvinces = 0x9D8;     // vector<int> begin
constexpr uint32_t kCountryProvincesEnd = 0x9DC;
constexpr uint32_t kCountryMoney = 0xE78;         // int64

// CPop
constexpr uint32_t kPopId = 0x0C;
constexpr uint32_t kPopSize = 0x58;
constexpr uint32_t kPopNext = 0x27C;  // next pop of the same type in the province

// MSVC std::vector<int> as laid out in the game.
struct IntVector {
    uint32_t begin, end, capacity;
    size_t size() const { return (end - begin) / 4; }
    uint32_t* data() const { return reinterpret_cast<uint32_t*>(begin); }
};

inline uint32_t read32(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(at); }
inline uintptr_t world() { return read32(hooks::exe_addr(kWorldPtrRva)); }
inline uintptr_t country_manager() { return read32(hooks::exe_addr(kCountryManagerPtrRva)); }
inline uint32_t& rng_calls() { return *reinterpret_cast<uint32_t*>(hooks::exe_addr(kRngCallsRva)); }

// Game day of `world`, or -1 without a world.
inline int32_t day(uintptr_t world) {
    return world ? static_cast<int32_t>(read32(world + kWorldDate)) / 24 : -1;
}

// The session manager of a running game, or 0 (no world, or the active idler is not the in-game one,
// whose vtable is the only one with the accessor).
inline uintptr_t session_manager(uintptr_t world) {
    if (!world) {
        return 0;
    }
    uintptr_t idler = read32(world + kWorldIdler);
    if (!idler || read32(idler) != hooks::exe_addr(kInGameIdlerVtableRva)) {
        return 0;
    }
    using GetFn = uintptr_t(__thiscall*)(uintptr_t);
    return reinterpret_cast<GetFn>(read32(read32(idler) + kIdlerSessionManagerSlot))(idler);
}

// Live entries of the player list: id != -1.
inline uintptr_t first_player(uintptr_t world) { return read32(world + kWorldPlayers); }
inline uintptr_t next_player(uintptr_t player) { return read32(player + 0x3C); }
inline int32_t player_id(uintptr_t player) { return static_cast<int32_t>(read32(player + 0x1C)); }
inline uint32_t player_date(uintptr_t player) { return read32(player + 0x34); }

}  // namespace cp::game
