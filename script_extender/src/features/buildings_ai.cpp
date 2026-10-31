#include "features/buildings_ai.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "game/buildings.h"
#include "game/localisation.h"
#include "game/objects.h"
#include "game/script.h"
#include "hooks/memory.h"
#include "log.h"

// The AI's construction job: docs/re/systems/buildings.md ("The AI builder").
//
// - AI_ConstructionUpdate (0x008569A0) is slot 6 of the job's vtable (entry 0x00E28504). It runs on the
//   host only, for AI countries; its body runs at most once a day (the job's +0x68 day of month). The
//   entry gets a wrapper: before the original, the job's cached fort / naval base / railroad (+0x7C /
//   +0x80 / +0x78, otherwise the first province building of type 4 / 3 / 2) are set to the buildings
//   named so; after it, when the body ran, the entries of common/buildings_ai.txt get their turn.
// - common/buildings_ai.txt (the first -mod= folder that has it, else the game's) is read on the first
//   run with the game's parser: one block per building, `chance`, `money`, and `weight`, a
//   CMeanTimeToHappen as a decision's ai_will_do is (factor / base, modifiers with their triggers).
// - Per entry and run: nothing at war (as vanilla); the chance is rolled; every owned province where
//   Province_CanExpandBuilding holds and the treasury covers `money` x the cost is weighted in a province
//   scope (as the daily province event pass makes it); one is drawn by weight and the construction is
//   posted as the vanilla builders post it (ConstructBuildingCommand, validated on every machine). The
//   draws come from a generator of our own seeded by the date, the country and the entry: the world's
//   and the global generators are left alone.

namespace se::features {

namespace {

using game::read;

constexpr uint32_t kJobUpdateSlot = 0x00E28504;  // slot 6 of the construction job's vtable 0x00E284EC
constexpr uint32_t kJobUpdate = 0x008569A0;      // AI_ConstructionUpdate, fastcall (job)
// The job
constexpr uint32_t kJobTag = 0x34;
constexpr uint32_t kJobCountry = 0x38;
constexpr uint32_t kJobLastDay = 0x68;  // day of month of the last run of the body
constexpr uint32_t kJobRailroad = 0x78;
constexpr uint32_t kJobFort = 0x7C;
constexpr uint32_t kJobNavalBase = 0x80;
// CBuilding type
constexpr uint32_t kBuildingType = 0x10;
constexpr int kTypeInfrastructure = 2;
constexpr int kTypeNavalBase = 3;
constexpr int kTypeFort = 4;
// CMeanTimeToHappen (vtable 0x00E2CEB0, 0x18 bytes): +4 0x18D, +8 base x1000, +0xC modifier vector.
// A modifier: a CAndTrigger (children +8/+0xC) with its factor x1000 at +0x20.
constexpr uint32_t kMtthVtable = 0x00E2CEB0;
constexpr uint32_t kMtthSize = 0x18;
constexpr uint32_t kMtthBase = 0x08;
constexpr uint32_t kMtthModifiers = 0x0C;
constexpr uint32_t kModifierChildren = 0x08;
constexpr uint32_t kModifierFactor = 0x20;
constexpr uint32_t kTriggerEvaluate = 0x18;  // trigger vt: bool (CEventScope*)
// CEventScope: a province scope as EventManager_DailyScan makes it
constexpr uint32_t kEventScopeCtor = 0x008A8650;  // thiscall (parent)
constexpr uint32_t kEventScopeSize = 0x48;
constexpr uint32_t kScopeCountryTag = 0x08;
constexpr uint32_t kScopeCountryIndex = 0x0C;
constexpr uint32_t kScopeProvince = 0x10;
constexpr uint32_t kNoTag = 0x2D2D2D;  // "---"
// Country, province, world
constexpr uint32_t kCountryTag = 0x1C;
constexpr uint32_t kCountryAtWar = 0x67C;
constexpr uint32_t kCountryProvinces = 0x9D8;  // vector<int> of province ids
constexpr uint32_t kCountryTreasury = 0xE78;   // i64, 2^15 fixed point
constexpr uint32_t kProvinceId = 0x58;
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kWorldDate = 0xB0C;
constexpr uint32_t kWorldHumans = 0xAEC;  // vector<int>: 0 = AI

constexpr const wchar_t* kFile = L"common\\buildings_ai.txt";
constexpr int kLoggedBuilds = 30;  // constructions logged one by one; then every 100th

struct Entry {
    std::string name;
    uintptr_t building = 0;
    int chance = 0;     // x1000, per run of the job
    int money = 2000;   // x1000: the treasury must cover this many times the cost
    uintptr_t weight = 0;  // CMeanTimeToHappen in game memory; 0 = every province weighs 1
};

uintptr_t g_original = 0;
std::vector<Entry> g_entries;
bool g_loaded = false;
int g_builds = 0;

int to_thousandths(const std::string& text) {
    return static_cast<int>(std::lround(std::strtod(text.c_str(), nullptr) * 1000.0));
}

uintptr_t building_named(const std::string& name) {
    game::Range buildings = game::province_buildings();
    for (uintptr_t* b = buildings.begin; b < buildings.end; ++b) {
        if (game::building_name(*b) == name) {
            return *b;
        }
    }
    return 0;
}

uintptr_t new_weight() {
    auto* mtth = static_cast<uint32_t*>(game::game_new(kMtthSize));
    if (!mtth) {
        return 0;
    }
    mtth[0] = static_cast<uint32_t>(hooks::exe_va(kMtthVtable));
    mtth[1] = 0x18D;
    mtth[2] = 1000;
    mtth[3] = mtth[4] = mtth[5] = 0;
    return reinterpret_cast<uintptr_t>(mtth);
}

// --- common/buildings_ai.txt ---------------------------------------------------------------------

class EntryBlock final : public game::ScriptBlock {
public:
    explicit EntryBlock(Entry& entry) : entry_(entry) {}

    void on_key(const std::string& key, uintptr_t parser) override {
        if (key == "weight" && game::value_is_block(parser)) {
            entry_.weight = new_weight();
            if (entry_.weight) {
                game::read_block(entry_.weight, parser);  // CMeanTimeToHappen's own keywords
                return;
            }
        } else if (key == "chance" && !game::value_is_block(parser)) {
            entry_.chance = to_thousandths(game::value_text(parser));
            return;
        } else if (key == "money" && !game::value_is_block(parser)) {
            entry_.money = to_thousandths(game::value_text(parser));
            return;
        }
        logf("buildings_ai: %s: unknown key '%s' ignored", entry_.name.c_str(), key.c_str());
        game::skip_value(parser);
    }

private:
    Entry& entry_;
};

class FileBlock final : public game::ScriptBlock {
public:
    void on_key(const std::string& key, uintptr_t parser) override {
        if (!game::value_is_block(parser)) {
            logf("buildings_ai: '%s' is not a block, ignored", key.c_str());
            return;
        }
        Entry entry;
        entry.name = key;
        EntryBlock block(entry);
        block.read(parser);
        entry.building = building_named(key);
        if (!entry.building) {
            logf("buildings_ai: no province building named '%s' in buildings.txt, ignored", key.c_str());
            return;
        }
        if (entry.chance <= 0) {
            logf("buildings_ai: %s has no chance > 0, ignored", key.c_str());
            return;
        }
        g_entries.push_back(entry);
    }
};

void load() {
    g_loaded = true;
    std::wstring file = game::game_file(kFile);
    std::string path = game::game_relative(file);
    if (path.empty()) {
        logf("buildings_ai: no common/buildings_ai.txt in the mod or the game; only the vanilla builders");
        return;
    }
    uintptr_t parser = game::parser_block(path);
    if (!parser) {
        logf("buildings_ai: could not open %s", path.c_str());
        return;
    }
    FileBlock block;
    block.read(parser);
    game::delete_parser_block(parser);
    for (const Entry& e : g_entries) {
        int modifiers = e.weight ? static_cast<int>((read<uintptr_t>(e.weight + kMtthModifiers + 4) -
                                                     read<uintptr_t>(e.weight + kMtthModifiers)) / 4)
                                 : 0;
        logf("buildings_ai: %s: chance %d/1000, money x%d/1000, %d weight modifiers", e.name.c_str(),
             e.chance, e.money, modifiers);
    }
    logf("buildings_ai: %u buildings from %s", static_cast<unsigned>(g_entries.size()), path.c_str());
}

// --- Choosing --------------------------------------------------------------------------------------

// splitmix64: the draws of one entry in one run, from the date, the country and the entry.
struct Rng {
    uint64_t state;
    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

// The weight (x1000) of the province: base, times the factor of every modifier whose triggers all hold
// (CDecision_EvaluateAiWillDo's rule; below 1 it is 0).
int64_t weigh(uintptr_t weight, uint32_t province_id) {
    if (!weight) {
        return 1000;
    }
    alignas(8) uint8_t scope[kEventScopeSize];
    using ScopeCtorFn = void(__fastcall*)(void*, void*, void*);
    reinterpret_cast<ScopeCtorFn>(hooks::exe_va(kEventScopeCtor))(scope, nullptr, nullptr);
    *reinterpret_cast<uint32_t*>(scope + kScopeCountryTag) = kNoTag;
    *reinterpret_cast<int*>(scope + kScopeCountryIndex) = 0;
    *reinterpret_cast<uint32_t*>(scope + kScopeProvince) = province_id;
    int64_t value = read<int>(weight + kMtthBase);
    using EvalFn = bool(__fastcall*)(uintptr_t, void*, void*);
    uintptr_t begin = read<uintptr_t>(weight + kMtthModifiers);
    uintptr_t end = read<uintptr_t>(weight + kMtthModifiers + 4);
    for (uintptr_t m = begin; m < end && value >= 1; m += 4) {
        uintptr_t modifier = read<uintptr_t>(m);
        uintptr_t child = read<uintptr_t>(modifier + kModifierChildren);
        uintptr_t children_end = read<uintptr_t>(modifier + kModifierChildren + 4);
        bool holds = true;
        for (; child < children_end && holds; child += 4) {
            uintptr_t trigger = read<uintptr_t>(child);
            holds = read<EvalFn>(read<uintptr_t>(trigger) + kTriggerEvaluate)(trigger, nullptr, scope);
        }
        if (holds) {
            value = value * read<int>(modifier + kModifierFactor) / 1000;
        }
    }
    return value >= 1 ? value : 0;
}

bool is_human(int index) {
    uintptr_t humans = game::world() + kWorldHumans;
    auto begin = read<const int*>(humans);
    auto end = read<const int*>(humans + 4);
    return index >= 0 && index < end - begin && begin[index] != 0;
}

std::string tag_text(uint32_t tag) { return std::string(reinterpret_cast<const char*>(&tag), 3); }

struct Candidate {
    uintptr_t province;
    int64_t weight;
    int64_t cost;
};

void run_entries(uintptr_t job) {
    if (!g_loaded) {
        load();
    }
    if (g_entries.empty()) {
        return;
    }
    int index = read<int>(job + kJobCountry);
    uintptr_t country = game::country(index);
    if (!country || is_human(index) || read<uint8_t>(country + kCountryAtWar)) {
        return;
    }
    uint32_t tag = read<uint32_t>(country + kCountryTag);
    uint32_t date = read<uint32_t>(game::world() + kWorldDate);
    int64_t spent = 0;  // posted this run: the commands pay only when they execute
    for (size_t e = 0; e < g_entries.size(); ++e) {
        const Entry& entry = g_entries[e];
        Rng rng{(static_cast<uint64_t>(date) << 32) ^ (static_cast<uint64_t>(index) << 12) ^ e};
        if (static_cast<int>(rng.next() % 1000) >= entry.chance) {
            continue;
        }
        int64_t treasury = read<int64_t>(country + kCountryTreasury) - spent;
        std::vector<Candidate> candidates;
        int64_t total = 0;
        game::Range ids = game::vector_at(country + kCountryProvinces);
        for (uintptr_t* id = ids.begin; id < ids.end; ++id) {
            uintptr_t province = game::province(static_cast<uint32_t>(*id));
            if (!province || read<int>(province + kProvinceOwner) != index ||
                !game::can_expand(country, entry.building, province)) {
                continue;
            }
            int64_t cost = game::expand_cost(country, entry.building, province);
            if (cost * entry.money / 1000 >= treasury) {
                continue;
            }
            int64_t weight = weigh(entry.weight, read<uint32_t>(province + kProvinceId));
            if (weight <= 0) {
                continue;
            }
            candidates.push_back({province, weight, cost});
            total += weight;
        }
        if (candidates.empty()) {
            continue;
        }
        int64_t pick = static_cast<int64_t>(rng.next() % static_cast<uint64_t>(total));
        const Candidate* chosen = &candidates.back();
        for (const Candidate& c : candidates) {
            if (pick < c.weight) {
                chosen = &c;
                break;
            }
            pick -= c.weight;
        }
        game::post_construction(entry.building, chosen->province, tag, index);
        spent += chosen->cost;
        ++g_builds;
        if (g_builds <= kLoggedBuilds) {
            logf("buildings_ai: %s builds %s in province %u (%u candidates)", tag_text(tag).c_str(),
                 entry.name.c_str(), read<uint32_t>(chosen->province + kProvinceId),
                 static_cast<unsigned>(candidates.size()));
        } else if (g_builds % 100 == 0) {
            logf("buildings_ai: %d constructions posted", g_builds);
        }
    }
}

// The vanilla builders' buildings by name: the job keeps what it finds first, and searches by type only
// when the field is empty.
void name_vanilla_buildings(uintptr_t job) {
    struct Slot {
        uint32_t field;
        const char* name;
        int type;
    };
    static const Slot kSlots[] = {
        {kJobRailroad, "railroad", kTypeInfrastructure},
        {kJobFort, "fort", kTypeFort},
        {kJobNavalBase, "naval_base", kTypeNavalBase},
    };
    for (const Slot& s : kSlots) {
        if (read<uintptr_t>(job + s.field)) {
            continue;
        }
        uintptr_t building = building_named(s.name);
        if (building && read<int>(building + kBuildingType) == s.type) {
            *reinterpret_cast<uintptr_t*>(job + s.field) = building;
        }
    }
}

void __fastcall construction_update(uintptr_t job, void* edx) {
    name_vanilla_buildings(job);
    int day = read<int>(job + kJobLastDay);
    reinterpret_cast<void(__fastcall*)(uintptr_t, void*)>(g_original)(job, edx);
    if (read<int>(job + kJobLastDay) != day) {  // the body ran (once a day at most)
        run_entries(job);
    }
}

}  // namespace

void install_buildings_ai(const Config& config) {
    if (!config.buildings_ai) {
        return;
    }
    g_original = hooks::exe_va(kJobUpdate);
    if (!hooks::replace_pointer(hooks::exe_va(kJobUpdateSlot), g_original,
                                reinterpret_cast<uintptr_t>(&construction_update))) {
        logf("buildings_ai: the construction job's vtable entry at %08X is not the expected one, off",
             kJobUpdateSlot);
        return;
    }
    logf("buildings_ai: on (common/buildings_ai.txt is read on the first AI construction run)");
}

}  // namespace se::features
