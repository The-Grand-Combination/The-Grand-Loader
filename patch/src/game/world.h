#pragma once

#include <cstdint>

#include "hooks/memory.h"

// v2game.exe 3.04 layout used by this plugin (docs/re/systems/multiplayer.md, tick.md, events.md).
// RVAs are VA - 0x00400000.

namespace cp::game {

constexpr uint32_t kWorldPtrRva = 0xE588E8;           // DAT_012588E8: CWorld*
constexpr uint32_t kCountryManagerPtrRva = 0xE587E4;  // DAT_012587E4: +0 count, +4 CCountry* array
constexpr uint32_t kOperatorNewRva = 0x6AE9AF;        // FUN_00AAE9AF, cdecl, malloc or throw
constexpr uint32_t kOperatorDeleteRva = 0x6AE91B;     // FUN_00AAE91B, cdecl, free

// CWorld
constexpr uint32_t kWorldRngTable = 0xB4;      // embedded MT19937 state (624 dwords)
constexpr uint32_t kWorldRngIndex = 0xA74;     // its index
constexpr uint32_t kWorldProvinces = 0xACC;    // vector<CProvince*> begin
constexpr uint32_t kWorldProvincesEnd = 0xAD0;
constexpr uint32_t kWorldDate = 0xB0C;         // hours
constexpr uint32_t kWorldSyncVector = 0xB74;   // vector<int> {begin +0, end +4, capacity +8}: the sync check
constexpr uint32_t kWorldOosShown = 0xBA8;     // byte: the OOS dialog was shown

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

inline uint32_t read32(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(at); }
inline uintptr_t world() { return read32(hooks::exe_addr(kWorldPtrRva)); }
inline uintptr_t country_manager() { return read32(hooks::exe_addr(kCountryManagerPtrRva)); }

}  // namespace cp::game
