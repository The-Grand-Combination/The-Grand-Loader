#include "game/map.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "game/localisation.h"
#include "game/objects.h"
#include "game/script.h"
#include "hooks/memory.h"
#include "log.h"

namespace se::game {

namespace {

constexpr uint32_t kCountryHasColonialRangeTo = 0x004BC020;  // EAX = CountryRef*, stack CProvince*, RET 4, AL

std::string lower_case(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool is_number(const std::string& word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Walks `words` as nested blocks: visit(depth, key, value) for every `key = value` (value empty when it is
// a block, which then opens at depth + 1), and bare words as (depth, word, "").
template <class Visit>
void walk_blocks(const std::vector<std::string>& words, Visit visit) {
    int depth = 0;
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w == "{") {
            ++depth;
        } else if (w == "}") {
            --depth;
        } else if (i + 1 < words.size() && words[i + 1] == "=") {
            bool block = i + 2 < words.size() && words[i + 2] == "{";
            visit(depth, w, block ? std::string() : (i + 2 < words.size() ? words[i + 2] : std::string()));
            i += block ? 1 : 2;  // the block's `{` is counted by the loop
        } else {
            visit(depth, w, std::string());
        }
    }
}

Continents load_continents() {
    Continents c;
    std::wstring path = game_file(L"map\\continent.txt");
    bool in_provinces = false;
    walk_blocks(script_words(read_file(path)), [&](int depth, const std::string& key, const std::string& value) {
        if (depth == 0 && value.empty()) {
            c.names.push_back(lower_case(key));
            in_provinces = false;
        } else if (depth == 1 && value.empty()) {
            in_provinces = key == "provinces";
        } else if (depth == 2 && in_provinces && !c.names.empty() && is_number(key)) {
            c.of_province[static_cast<uint32_t>(std::strtoul(key.c_str(), nullptr, 10))] = static_cast<int>(c.names.size()) - 1;
        }
    });
    logf("map: %u continents, %u provinces in %ls", static_cast<unsigned>(c.names.size()),
         static_cast<unsigned>(c.of_province.size()), path.c_str());
    return c;
}

std::map<uint32_t, Position> load_positions() {
    std::map<uint32_t, Position> p;
    std::wstring path = game_file(L"map\\positions.txt");
    uint32_t province = 0;
    bool in_unit = false;
    walk_blocks(script_words(read_file(path)), [&](int depth, const std::string& key, const std::string& value) {
        if (depth == 0 && value.empty()) {
            province = static_cast<uint32_t>(std::strtoul(key.c_str(), nullptr, 10));
        } else if (depth == 1 && value.empty()) {
            in_unit = key == "unit";
        } else if (depth == 2 && in_unit && province && (key == "x" || key == "y")) {
            Position& pos = p[province];
            (key == "x" ? pos.x : pos.y) = static_cast<float>(std::atof(value.c_str()));
        }
    });
    logf("map: %u province positions from %ls", static_cast<unsigned>(p.size()), path.c_str());
    return p;
}

int load_badboy_limit() {
    std::string text = read_file(game_file(L"common\\defines.lua"));
    size_t at = text.find("BADBOY_LIMIT");
    at = at == std::string::npos ? at : text.find('=', at);
    return at == std::string::npos ? 25000 : static_cast<int>(std::atof(text.c_str() + at + 1) * 1000);
}

}  // namespace

std::vector<uint32_t> state_provinces(uintptr_t state) {
    std::vector<uint32_t> out;
    uintptr_t end = read<uintptr_t>(state + kStateProvinceIds + 4);
    for (uintptr_t p = read<uintptr_t>(state + kStateProvinceIds); p < end; p += 4) {
        out.push_back(read<uint32_t>(p));
    }
    return out;
}

std::vector<uint32_t> owned_provinces(int country) {
    std::vector<uint32_t> out;
    uintptr_t c = country > 0 ? game::country(country) : 0;
    if (!c) {
        return out;
    }
    uintptr_t end = read<uintptr_t>(c + kCountryProvinces + 4);
    for (uintptr_t p = read<uintptr_t>(c + kCountryProvinces); p < end; p += 4) {
        out.push_back(read<uint32_t>(p));
    }
    return out;
}

bool country_exists(int country) {
    uintptr_t c = country > 0 ? game::country(country) : 0;
    return c && read<uintptr_t>(c + kCountryProvinces) != read<uintptr_t>(c + kCountryProvinces + 4);
}

std::vector<uint32_t> neighbours(uint32_t province) {
    std::vector<uint32_t> out;
    uintptr_t geo = read<uintptr_t>(game::province(province) + kProvinceGeo);
    if (!geo) {
        return out;
    }
    uintptr_t end = read<uintptr_t>(geo + kGeoNeighbours + 4);
    for (uintptr_t e = read<uintptr_t>(geo + kGeoNeighbours); e < end; e += kNeighbourEntry) {
        int kind = read<int>(e);
        uint32_t id = read<uint32_t>(e + 4);
        if (kind == kNeighbourLand && id) {
            out.push_back(id);
        }
    }
    return out;
}

bool is_colonial_state(uintptr_t state) { return read<int>(state + kStateColonyStatus) > 0; }

bool is_coastal(uint32_t province) { return read<int>(game::province(province) + kProvinceCoastal) != 0; }

bool is_coastal_state(uintptr_t state) {
    std::vector<uint32_t> ids = state_provinces(state);
    return std::any_of(ids.begin(), ids.end(), is_coastal);
}

int homeland_states(int country) {
    int n = 0;
    for (uintptr_t node = read<uintptr_t>(game::country(country) + kCountryStates); node; node = read<uintptr_t>(node + 8)) {
        uintptr_t state = read<uintptr_t>(node);
        n += state && !is_colonial_state(state) ? 1 : 0;
    }
    return n;
}

int components(std::vector<uint32_t> provinces) {
    std::sort(provinces.begin(), provinces.end());
    std::vector<bool> left(provinces.size(), true);
    int n = 0;
    for (size_t start = 0; start < provinces.size(); ++start) {
        if (!left[start]) {
            continue;
        }
        n++;
        left[start] = false;
        std::vector<uint32_t> group{provinces[start]};
        for (size_t i = 0; i < group.size(); ++i) {
            for (uint32_t other : neighbours(group[i])) {
                auto it = std::lower_bound(provinces.begin(), provinces.end(), other);
                size_t at = static_cast<size_t>(it - provinces.begin());
                if (it != provinces.end() && *it == other && left[at]) {
                    left[at] = false;
                    group.push_back(other);
                }
            }
        }
    }
    return n;
}

const Continents& continents() {
    static const Continents c = load_continents();
    return c;
}

int continent_of(uint32_t province) {
    const Continents& c = continents();
    auto it = c.of_province.find(province);
    return it == c.of_province.end() ? -1 : it->second;
}

int capital_continent(int country) {
    return country_exists(country) ? continent_of(read<uint32_t>(game::country(country) + kCountryCapital)) : -1;
}

const std::map<uint32_t, Position>& positions() {
    static const std::map<uint32_t, Position> p = load_positions();
    return p;
}

float distance_to_country(const std::vector<uintptr_t>& states, int country) {
    const std::map<uint32_t, Position>& pos = positions();
    std::vector<const Position*> theirs;
    for (uint32_t id : owned_provinces(country)) {
        auto it = pos.find(id);
        if (it != pos.end()) {
            theirs.push_back(&it->second);
        }
    }
    float best = -1;
    for (uintptr_t state : states) {
        for (uint32_t id : state_provinces(state)) {
            auto here = pos.find(id);
            if (here == pos.end()) {
                continue;
            }
            for (const Position* there : theirs) {
                float dx = here->second.x - there->x, dy = here->second.y - there->y;
                float d = dx * dx + dy * dy;
                best = best < 0 || d < best ? d : best;
            }
        }
    }
    return best < 0 ? -1.0f : std::sqrt(best);
}

int badboy_limit_x1000() {
    static const int limit = load_badboy_limit();
    return limit;
}

bool in_colonial_range(uint32_t province_id, int country) {
    uint32_t ref[2] = {read<uint32_t>(game::country(country) + kCountryTag), static_cast<uint32_t>(country)};
    uintptr_t province = game::province(province_id);
    uintptr_t f = hooks::exe_va(kCountryHasColonialRangeTo);
    uint8_t result = 0;
    __asm {
        lea eax, ref
        push province
        call f
        mov result, al
    }
    return result != 0;
}

}  // namespace se::game
