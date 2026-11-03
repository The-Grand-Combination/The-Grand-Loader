#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "hooks/memory.h"

// Reading game objects. Layouts and addresses: docs/re/symbols (structs.csv, globals.csv).

namespace se::game {

constexpr uint32_t kWorld = 0x012588E8;           // g_world
constexpr uint32_t kCountryManager = 0x012587E4;  // g_countryManager: +4 CCountry* array
constexpr uint32_t kBuildingDb = 0x0125CE80;      // g_buildingDb

// World
constexpr uint32_t kWorldProvinces = 0xACC;  // CProvince** indexed by province id
constexpr uint32_t kWorldIdler = 0xB24;      // CInGameIdler*
constexpr uint32_t kWorldPlayerTag = 0xB5C;
constexpr uint32_t kWorldPlayerIndex = 0xB60;
// g_buildingDb, CBuilding
constexpr uint32_t kDbProvinceBuildings = 0x1C;  // vector<CBuilding*>, indexed by CBuilding+0x134
constexpr uint32_t kBuildingName = 0x20;         // std::string (VS2010)

template <class T>
T read(uintptr_t at) {
    return *reinterpret_cast<const T*>(at);
}

// A game vector of pointers (begin, end).
struct Range {
    uintptr_t* begin;
    uintptr_t* end;
    size_t size() const { return static_cast<size_t>(end - begin); }
};

inline Range vector_at(uintptr_t at) { return {read<uintptr_t*>(at), read<uintptr_t*>(at + 4)}; }

inline uintptr_t world() { return read<uintptr_t>(hooks::exe_va(kWorld)); }

inline uintptr_t country(int index) {
    uintptr_t countries = read<uintptr_t>(read<uintptr_t>(hooks::exe_va(kCountryManager)) + 4);
    return read<uintptr_t>(countries + static_cast<uint32_t>(index) * 4);
}

inline uintptr_t province(uint32_t id) { return read<uintptr_t>(read<uintptr_t>(world() + kWorldProvinces) + id * 4); }

// The province buildings of buildings.txt in file order (empty before the game data is loaded).
inline Range province_buildings() {
    uintptr_t db = read<uintptr_t>(hooks::exe_va(kBuildingDb));
    return db ? vector_at(db + kDbProvinceBuildings) : Range{nullptr, nullptr};
}

// VS2010 std::string as the game lays it out: inline below 16 characters, else by pointer; 0x1C bytes.
// Built here only for the game to read; never handed to the game to destroy.
struct GameString {
    union {
        char buf[16];
        const char* ptr;
    };
    uint32_t size;
    uint32_t capacity;
    uint32_t allocator;

    // A view of `s`, which must outlive every use of this object.
    explicit GameString(const std::string& s) : size(static_cast<uint32_t>(s.size())), allocator(0) {
        if (s.size() < 16) {
            std::memcpy(buf, s.c_str(), s.size() + 1);
            capacity = 15;
        } else {
            ptr = s.c_str();
            capacity = size;
        }
    }

    // A copy of the game string at `at`.
    static std::string copy(uintptr_t at) {
        auto length = read<uint32_t>(at + 0x10);
        const char* text = read<uint32_t>(at + 0x14) > 15 ? read<const char*>(at) : reinterpret_cast<const char*>(at);
        return std::string(text, length);
    }
};
static_assert(sizeof(GameString) == 0x1C, "VS2010 std::string is 28 bytes");

inline std::string building_name(uintptr_t building) { return GameString::copy(building + kBuildingName); }

}  // namespace se::game
