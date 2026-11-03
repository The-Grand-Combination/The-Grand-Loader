#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Provinces, states and the map as the land rules of the Script Extender read them: adjacency, coasts,
// colonial status, continents and positions (from the mod's map files), the game's colonial range.
// Offsets: docs/re/systems/buildings.md ("Fort"), colonisation.md, symbols/structs.csv. Reads only.

namespace se::game {

constexpr uint32_t kProvinceId = 0x58;
constexpr uint32_t kProvinceGeo = 0x5C;       // geography: terrain and adjacency
constexpr uint32_t kGeoNeighbours = 0xC8;     // vector of kNeighbourEntry-byte entries {kind, province id, ...}; end +0xCC
constexpr uint32_t kNeighbourEntry = 0x24;
constexpr int kNeighbourLand = 1;
constexpr int kNeighbourAcrossWater = 2;  // coastal provinces of neighbouring seas (I): not a border
constexpr uint32_t kProvinceOwner = 0x12C;    // country index
constexpr uint32_t kProvinceCoastal = 0x158;  // non-zero: a port can be built
constexpr uint32_t kProvinceState = 0x188;    // CState*
constexpr uint32_t kStateProvinceIds = 0x48;  // vector<int>
constexpr uint32_t kStateColonyStatus = 0x84; // > 0: colonial (CIsColonialTrigger_Evaluate)
constexpr uint32_t kCountryTag = 0x1C;
constexpr uint32_t kCountryProvinces = 0x9D8;  // vector<int>; empty = the country does not exist
constexpr uint32_t kCountryCapital = 0xBE4;    // province id
constexpr uint32_t kCountryStates = 0xE44;     // list {state, ?, next}
constexpr uint32_t kCountryStateCount = 0xE4C;

// The province ids of a state, of a country.
std::vector<uint32_t> state_provinces(uintptr_t state);
std::vector<uint32_t> owned_provinces(int country);
bool country_exists(int country);  // owns a province

// The provinces a province borders by land (kind 1 of the entries AI_PickFortProvince walks; kind 2 links
// coastal provinces across water and is left out).
std::vector<uint32_t> neighbours(uint32_t province);

bool is_colonial_state(uintptr_t state);
bool is_coastal(uint32_t province);
bool is_coastal_state(uintptr_t state);
// The country's states that are not colonial.
int homeland_states(int country);

// Connected groups (by land borders) that `provinces` form.
int components(std::vector<uint32_t> provinces);

// map/continent.txt (the mod's, else the game's): `<name> = { provinces = { ids } ... }`, read once.
struct Continents {
    std::vector<std::string> names;       // lower case, in file order
    std::map<uint32_t, int> of_province;  // province id -> index into names
};
const Continents& continents();
int continent_of(uint32_t province);     // -1 when not listed
int capital_continent(int country);      // -1 without a listed capital

// map/positions.txt (the mod's, else the game's): the `unit` position of every province, read once.
struct Position {
    float x, y;
};
const std::map<uint32_t, Position>& positions();
// The shortest distance in map units between a province of `states` and a province of `country`, or -1.
float distance_to_country(const std::vector<uintptr_t>& states, int country);

// BADBOY_LIMIT of common/defines.lua (the mod's, else the game's), x1000; 25000 when it cannot be read.
int badboy_limit_x1000();

// Country_HasColonialRangeTo (0x004BC020): one of the country's controlled, unblockaded provinces with a
// naval base is within 100 + effective level x colonial_range of the province (colonisation.md).
bool in_colonial_range(uint32_t province, int country);

}  // namespace se::game
