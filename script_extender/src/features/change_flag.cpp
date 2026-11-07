#include "features/change_flag.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "game/effects.h"
#include "game/localisation.h"
#include "game/objects.h"
#include "game/script.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Flags: docs/re/systems/flags.md ("Drawing a flag", "Extra flags").
//
// - Every flag the game draws (shields, map, ledger, events, chat...) goes through Country_GetFlagTexture
//   (0x0052FC10: EAX = CCountry*, returns the index of a flag atlas in g_flagOwner+0x6BD6C, plain RET).
//   The caller then takes atlases[index] and the country's cell from the atlas's cell table (+0x14, by the
//   country's index +0x20) and cell size (1 / columns +0x38, 1 / rows +0x34).
// - Its entry (`56 8B F0 8B 8E D0 0B 00 00`: PUSH ESI; MOV ESI,EAX; MOV ECX,[ESI+0xBD0]) jumps to
//   flag_texture_thunk. For a country with an extra flag it returns the index of a one-cell atlas of the
//   plugin's: the file's texture (D3DX at its own size, as the single flags), one column and one row,
//   and a cell table whose every cell is at (0, 0), so any country's cell is the whole texture. Every other
//   country goes to the original. One atlas per file, appended to the game's atlas vector when first drawn.
// - `change_flag` is built as a set_country_flag effect (token 0x40B, the value as a std::string at +0x20)
//   with the plugin's vtable. Execute keeps the choice as the country flag `se_flag_<path, / as __>`, set
//   and cleared with set_country_flag's and clr_country_flag's own Execute, so the save holds it and
//   anything watching those effects (cull_decision_checks) sees it. The flags are read back per country
//   when drawn; a save load (the CALL EconomyManager_UpdateFull at 0x0066321D in
//   Game_FinalizeLoadAndEnterGameplay) makes them be read again.
// Display only: the effect changes nothing but a country flag, the same on every machine.

namespace se::features {

namespace {

using game::GameString;
using game::read;

constexpr const char* kKeyword = "change_flag";
constexpr const char* kDefault = "default";
constexpr const char* kFlagPrefix = "se_flag_";
constexpr const char* kSeparator = "__";  // '/' in the flag name
constexpr const char* kExtraFolder = "/flags/extra/";
constexpr const char* kExtensions[] = {".dds", ".tga"};

// Effects (docs/re/systems/effects.md)
constexpr int kTokenSetCountryFlag = 0x40B;
constexpr uint32_t kSetCountryFlagVtable = 0x00E2AC88;  // CSetCountryFlagEffect, 0x3C bytes
constexpr uint32_t kClrCountryFlagVtable = 0x00E2ACC0;  // CClrCountryFlagEffect, 0x3C bytes
constexpr uint32_t kEffectBytes = 0x3C;
constexpr uint32_t kEffectName = 0x20;  // std::string
constexpr uint32_t kCbManagerLoadCall = 0x0063DCD2;  // the plugin's tokens are registered before it
constexpr uint32_t kCbManagerLoad = 0x0090CDC0;
constexpr uint32_t kStringAssign = 0x00409350;  // thiscall (const char*, length), RET 8

// Countries and their flags
constexpr uint32_t kScopeCountryIndex = 0x0C;
constexpr uint32_t kCountryIndex = 0x20;
constexpr uint32_t kCountryFlags = 0x1B0;  // CFlags
constexpr uint32_t kFlagsRoot = 0x04;      // tree nodes {entry, ?, left +0x08, next +0x0C, right +0x10}
constexpr uint32_t kNodeLeft = 0x08;
constexpr uint32_t kNodeNext = 0x0C;
constexpr uint32_t kNodeRight = 0x10;
constexpr uint32_t kFlagSet = 0x1C;  // u8 in the entry (name std::string at +0)

// Drawing
constexpr uint32_t kFlagTexture = 0x0052FC10;  // Country_GetFlagTexture
constexpr unsigned char kFlagTextureEntry[] = {0x56, 0x8B, 0xF0, 0x8B, 0x8E, 0xD0, 0x0B, 0x00, 0x00};
constexpr uint32_t kFlagOwner = 0x012589B8;  // g_flagOwner: the atlases and the texture manager
constexpr uint32_t kOwnerDevice = 0x64;      // IDirect3DDevice9*
constexpr uint32_t kOwnerAtlases = 0x6BD6C;  // vector<FlagAtlas*> {begin, end, capacity}
// D3DXCreateTextureFromFileInMemoryEx, the import the flag builder uses for the single flags (0x007E55CC).
// Not TextureManager_GetOrLoad: it rounds the size up to a power of two without stretching (filter NONE),
// so a 93x64 flag fills 93/128 of its texture and the one-cell atlas would show the black rest.
constexpr uint32_t kIatCreateTextureFromFileInMemoryEx = 0x00C8A424;
constexpr uint32_t kD3dxDefault = 0xFFFFFFFF;
constexpr uint32_t kD3dxDefaultNonPow2 = 0xFFFFFFFE;
constexpr uint32_t kPoolManaged = 1;
constexpr uint32_t kFinalizeEconomyCall = 0x0066321D;  // CALL EconomyManager_UpdateFull, once per load
constexpr uint32_t kEconomyUpdateFull = 0x0068BF00;
constexpr uint32_t kPhysfsExists = 0x00B21820;  // cdecl (const char* path)
constexpr uint32_t kSearchPaths = 0x012586E0;   // g_searchPaths (see event_background.cpp)
constexpr uint32_t kSearchMode = 0x4C;
constexpr uint32_t kGameFolders = 0x84;
constexpr uint32_t kModFolders = 0x244;
constexpr uint32_t kFolderSize = 0x1C;
constexpr uint32_t kGfxFolder = 2;

// FlagAtlas (0x44 bytes)
constexpr uint32_t kAtlasBytes = 0x44;
constexpr uint32_t kAtlasTexture = 0x00;
constexpr uint32_t kAtlasCells = 0x14;    // vector of rows (16 bytes: {begin, end, ...} of {u, v} floats)
constexpr uint32_t kAtlasHasFlag = 0x24;  // vector<int> by country index: 1 = the country has a cell
constexpr uint32_t kAtlasRows = 0x34;
constexpr uint32_t kAtlasColumns = 0x38;
constexpr uint32_t kAtlasCellWidth = 0x3C;
constexpr uint32_t kAtlasCellHeight = 0x40;
constexpr uint32_t kCells = 8192;  // cells of the single row: more than there are country indices

int g_token = 0;
uintptr_t g_prev_load = 0;
uintptr_t g_prev_finalize = 0;
uintptr_t g_flag_texture_trampoline = 0;
uintptr_t g_vtable_storage[game::kEffectSlots + 1];
uintptr_t g_vtable = 0;

// Per country index: the extra flag's atlas, read from its flags. Function-local statics: first used on
// the game thread, never under the loader lock (docs/CODING.md, 1). Effects and drawing both run on the
// main thread; the mutex covers anything else.
struct CountryFlag {
    bool known = false;
    uintptr_t root = 0;  // the CFlags tree root it was read from: a new tree is read again
    int atlas = -1;
};
std::mutex& g_mutex() { static std::mutex v; return v; }
std::vector<CountryFlag>& g_countries() { static std::vector<CountryFlag> v; return v; }
std::map<std::string, int>& g_atlases() { static std::map<std::string, int> v; return v; }  // path -> atlas, -1 = none
std::set<std::string>& g_reported() { static std::set<std::string> v; return v; }

// --- Names -----------------------------------------------------------------------------------------

// `value` as a path under gfx/flags/extra without extension, or empty when it is not one: letters,
// digits, '_' and '/' only, no empty folder, no "__" (it stands for '/' in the flag name).
std::string normalise(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    for (char& c : value) {
        if (c == '\\') {
            c = '/';
        }
    }
    for (const char* ext : kExtensions) {
        size_t n = std::strlen(ext);
        if (value.size() > n && _stricmp(value.c_str() + value.size() - n, ext) == 0) {
            value.resize(value.size() - n);
        }
    }
    if (value.empty() || value.front() == '/' || value.back() == '/' || value.find("//") != std::string::npos ||
        value.find(kSeparator) != std::string::npos) {
        return {};
    }
    for (char c : value) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/';
        if (!ok) {
            return {};
        }
    }
    return value;
}

std::string flag_name(const std::string& path) {
    std::string name = kFlagPrefix;
    for (char c : path) {
        if (c == '/') {
            name += kSeparator;
        } else {
            name += c;
        }
    }
    return name;
}

std::string path_of(const std::string& name) {
    std::string path;
    for (size_t i = std::strlen(kFlagPrefix); i < name.size(); ++i) {
        if (name.compare(i, 2, kSeparator) == 0) {
            path += '/';
            ++i;
        } else {
            path += name[i];
        }
    }
    return path;
}

void report_once(const std::string& what) {
    if (g_reported().insert(what).second) {
        logf("change_flag: %s", what.c_str());
    }
}

// The names of the set `se_flag_` flags of a CFlags tree (CFlags_WriteSet's walk).
void collect(uintptr_t node, std::vector<std::string>& out, int depth) {
    while (node && depth < 4096) {
        if (uintptr_t left = read<uintptr_t>(node + kNodeLeft)) {
            collect(left, out, depth + 1);
        }
        uintptr_t entry = read<uintptr_t>(node);
        if (entry && read<uint8_t>(entry + kFlagSet)) {
            std::string name = GameString::copy(entry);
            if (name.compare(0, std::strlen(kFlagPrefix), kFlagPrefix) == 0) {
                out.push_back(name);
            }
        }
        if (uintptr_t right = read<uintptr_t>(node + kNodeRight)) {
            collect(right, out, depth + 1);
        }
        node = read<uintptr_t>(node + kNodeNext);
        ++depth;
    }
}

std::vector<std::string> set_flags(uintptr_t country) {
    std::vector<std::string> names;
    collect(read<uintptr_t>(country + kCountryFlags + kFlagsRoot), names, 0);
    return names;
}

// --- Atlases ---------------------------------------------------------------------------------------

bool physfs_exists(const std::string& path) {
    return reinterpret_cast<int(__cdecl*)(const char*)>(hooks::exe_va(kPhysfsExists))(path.c_str()) != 0;
}

// The file of `path` as the game finds a picture: the mod's gfx folder, then the game's; .dds first.
std::string find_file(const std::string& path) {
    uintptr_t paths = read<uintptr_t>(hooks::exe_va(kSearchPaths));
    if (!paths) {
        return {};
    }
    int mode = read<int>(paths + kSearchMode);
    std::vector<std::string> roots;
    if (mode == 1 || mode == 2) {
        roots.push_back(GameString::copy(paths + kModFolders + kGfxFolder * kFolderSize));
    }
    if (mode != 1) {
        roots.push_back(GameString::copy(paths + kGameFolders + kGfxFolder * kFolderSize));
    }
    for (const std::string& root : roots) {
        for (const char* ext : kExtensions) {
            std::string file = root + kExtraFolder + path + ext;
            if (physfs_exists(file)) {
                return file;
            }
        }
    }
    return {};
}

// The whole file at the PhysFS path `file` (the mounted mod folders over the game's: a mod's file can be
// found under the game's path), through the exe's PhysFS exports; empty on failure.
std::vector<char> read_physfs(const std::string& file) {
    HMODULE exe = GetModuleHandleW(nullptr);
    using OpenFn = void*(__cdecl*)(const char*);
    using LengthFn = long long(__cdecl*)(void*);
    using ReadFn = long long(__cdecl*)(void*, void*, unsigned, unsigned);
    using CloseFn = int(__cdecl*)(void*);
    auto open = reinterpret_cast<OpenFn>(GetProcAddress(exe, "PHYSFS_openRead"));
    auto length = reinterpret_cast<LengthFn>(GetProcAddress(exe, "PHYSFS_fileLength"));
    auto read_fn = reinterpret_cast<ReadFn>(GetProcAddress(exe, "PHYSFS_read"));
    auto close = reinterpret_cast<CloseFn>(GetProcAddress(exe, "PHYSFS_close"));
    std::vector<char> data;
    void* handle = open && length && read_fn && close ? open(file.c_str()) : nullptr;
    if (!handle) {
        return data;
    }
    long long size = length(handle);
    if (size > 0 && size < (64 << 20)) {
        data.resize(static_cast<size_t>(size));
        if (read_fn(handle, data.data(), 1, static_cast<unsigned>(size)) != size) {
            data.clear();
        }
    }
    close(handle);
    return data;
}

// The file at `file` (a PhysFS path) as a texture of the image's own size (stretched to a power of two
// only where the device needs it), MANAGED; 0 on failure. Kept for the session.
uintptr_t load_texture(uintptr_t owner, const std::string& file) {
    std::vector<char> data = read_physfs(file);
    void* device = read<void*>(owner + kOwnerDevice);
    if (data.empty() || !device) {
        logf("change_flag: %s: %s", file.c_str(), data.empty() ? "could not be read" : "no device");
        return 0;
    }
    using LoadFn = HRESULT(WINAPI*)(void*, const void*, UINT, UINT, UINT, UINT, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    DWORD, void*, void*, void**);
    auto load = read<LoadFn>(hooks::exe_va(kIatCreateTextureFromFileInMemoryEx));
    void* texture = nullptr;
    HRESULT hr = load(device, data.data(), static_cast<UINT>(data.size()), kD3dxDefaultNonPow2, kD3dxDefaultNonPow2,
                      kD3dxDefault, 0, 0 /* D3DFMT_UNKNOWN: from the file */, kPoolManaged, kD3dxDefault, kD3dxDefault,
                      0, nullptr, nullptr, &texture);
    if (FAILED(hr)) {
        logf("change_flag: %s: D3DX error %08lX", file.c_str(), static_cast<unsigned long>(hr));
        return 0;
    }
    return reinterpret_cast<uintptr_t>(texture);
}

// The cell tables every plugin atlas shares (read only): one row of kCells cells at (0, 0), and every
// country index marked as having a cell.
uintptr_t shared_rows() {
    static float* cells = static_cast<float*>(game::game_new(kCells * 2 * sizeof(float)));
    static uintptr_t* row = static_cast<uintptr_t*>(game::game_new(4 * sizeof(uintptr_t)));
    static bool filled = false;
    if (!filled) {
        std::memset(cells, 0, kCells * 2 * sizeof(float));
        row[0] = reinterpret_cast<uintptr_t>(cells);
        row[1] = row[2] = reinterpret_cast<uintptr_t>(cells + kCells * 2);
        row[3] = 0;
        filled = true;
    }
    return reinterpret_cast<uintptr_t>(row);
}

uintptr_t shared_has_flag() {
    static int* ones = static_cast<int*>(game::game_new(kCells * sizeof(int)));
    static bool filled = false;
    if (!filled) {
        for (uint32_t i = 0; i < kCells; ++i) {
            ones[i] = 1;
        }
        filled = true;
    }
    return reinterpret_cast<uintptr_t>(ones);
}

// Appends `atlas` to the game's atlas vector (game heap) and returns its index.
int append_atlas(uintptr_t owner, uintptr_t atlas) {
    auto* vec = reinterpret_cast<uintptr_t**>(owner + kOwnerAtlases);  // {begin, end, capacity}
    size_t size = static_cast<size_t>(vec[1] - vec[0]);
    if (vec[1] == vec[2]) {
        size_t capacity = size ? size * 2 : 16;
        auto* grown = static_cast<uintptr_t*>(game::game_new(capacity * sizeof(uintptr_t)));
        if (size) {
            std::memcpy(grown, vec[0], size * sizeof(uintptr_t));
        }
        uintptr_t* old = vec[0];
        vec[0] = grown;
        vec[1] = grown + size;
        vec[2] = grown + capacity;
        if (old) {
            game::game_delete(old);
        }
    }
    *vec[1] = atlas;
    vec[1] = vec[1] + 1;
    return static_cast<int>(size);
}

// The atlas of the extra flag `path`, made on first use; -1 when the file is missing or does not load.
int atlas_for(const std::string& path) {
    auto it = g_atlases().find(path);
    if (it != g_atlases().end()) {
        return it->second;
    }
    int index = -1;
    uintptr_t owner = read<uintptr_t>(hooks::exe_va(kFlagOwner));
    std::string file = owner ? find_file(path) : std::string();
    if (owner && file.empty()) {
        report_once("no gfx/flags/extra/" + path + ".dds or .tga; the government's flag is shown");
    }
    uintptr_t texture = file.empty() ? 0 : load_texture(owner, file);
    if (!file.empty() && !texture) {
        report_once(file + " did not load; the government's flag is shown");
    }
    uintptr_t first = owner ? read<uintptr_t>(read<uintptr_t>(owner + kOwnerAtlases)) : 0;
    if (texture && first) {
        auto* atlas = static_cast<uint8_t*>(game::game_new(kAtlasBytes));
        std::memset(atlas, 0, kAtlasBytes);
        uintptr_t rows = shared_rows();
        uintptr_t has_flag = shared_has_flag();
        auto put = [&](uint32_t at, uintptr_t v) { std::memcpy(atlas + at, &v, sizeof(v)); };
        put(kAtlasTexture, texture);
        put(kAtlasCells, rows);
        put(kAtlasCells + 4, rows + 16);
        put(kAtlasCells + 8, rows + 16);
        put(kAtlasHasFlag, has_flag);
        put(kAtlasHasFlag + 4, has_flag + kCells * sizeof(int));
        put(kAtlasHasFlag + 8, has_flag + kCells * sizeof(int));
        put(kAtlasRows, 1);
        put(kAtlasColumns, 1);
        put(kAtlasCellWidth, read<uint32_t>(first + kAtlasCellWidth));
        put(kAtlasCellHeight, read<uint32_t>(first + kAtlasCellHeight));
        index = append_atlas(owner, reinterpret_cast<uintptr_t>(atlas));
        logf("change_flag: %s loaded as flag atlas %d", file.c_str(), index);
    }
    g_atlases()[path] = index;
    return index;
}

// The atlas to draw `country` with, or -1 for the government's flag.
int __stdcall extra_flag_atlas(uintptr_t country) {
    if (!country) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(g_mutex());
    int index = read<int>(country + kCountryIndex);
    if (index < 0 || index >= static_cast<int>(kCells)) {
        return -1;
    }
    if (static_cast<size_t>(index) >= g_countries().size()) {
        g_countries().resize(static_cast<size_t>(index) + 1);
    }
    CountryFlag& c = g_countries()[static_cast<size_t>(index)];
    uintptr_t root = read<uintptr_t>(country + kCountryFlags + kFlagsRoot);
    if (!c.known || c.root != root) {
        std::vector<std::string> names = set_flags(country);
        c.atlas = names.empty() ? -1 : atlas_for(path_of(names.front()));
        c.root = root;
        c.known = true;
    }
    return c.atlas;
}

void __stdcall forget_flags() {
    std::lock_guard<std::mutex> lock(g_mutex());
    g_countries().clear();
}

// Country_GetFlagTexture's entry: EAX = country; the result in EAX, plain RET.
__declspec(naked) void flag_texture_thunk() {
    __asm {
        SE_SAVE_ALL
        push eax
        call extra_flag_atlas
        test eax, eax
        js vanilla
        mov [esp + SE_SAVED_EAX], eax
        SE_RESTORE_ALL
        ret
    vanilla:
        SE_RESTORE_ALL
        jmp g_flag_texture_trampoline
    }
}

// The CALL EconomyManager_UpdateFull of Game_FinalizeLoadAndEnterGameplay: the flags are read again.
__declspec(naked) void finalize_thunk() {
    __asm {
        SE_SAVE_ALL
        call forget_flags
        SE_RESTORE_ALL
        jmp g_prev_finalize
    }
}

// --- The effect ------------------------------------------------------------------------------------

// Runs set_country_flag's or clr_country_flag's Execute for the flag `name` in `scope`'s country, on an
// object with only the name filled in (all they read besides the scope).
// Slot 11 of `vtable` as it is now (another plugin may have replaced it after we were installed).
uintptr_t execute_slot(uint32_t vtable) { return read<uintptr_t>(hooks::exe_va(vtable) + game::kEffectSlotExecute * 4); }

void run_flag_effect(uintptr_t execute, const std::string& name, uintptr_t scope, int arg) {
    alignas(4) uint8_t effect[kEffectBytes] = {};
    GameString game_name(name);
    std::memcpy(effect + kEffectName, &game_name, sizeof(game_name));
    using ExecuteFn = void(__fastcall*)(uintptr_t, void*, uintptr_t, int);
    reinterpret_cast<ExecuteFn>(execute)(reinterpret_cast<uintptr_t>(effect), nullptr, scope, arg);
}

// Slot 11, Execute: thiscall (scope, ?), RET 8.
void __fastcall execute(uintptr_t effect, void*, uintptr_t scope, int arg) {
    std::string value = GameString::copy(effect + kEffectName);
    bool to_default = _stricmp(value.c_str(), kDefault) == 0;
    std::string path = to_default ? std::string() : normalise(value);
    if (!to_default && path.empty()) {
        report_once("\"" + value + "\" is not a path under gfx/flags/extra (letters, digits, _ and /); ignored");
        return;
    }
    int index = read<int>(scope + kScopeCountryIndex);
    if (index <= 0) {
        return;
    }
    uintptr_t country = game::country(index);
    std::string target = to_default ? std::string() : flag_name(path);
    for (const std::string& name : set_flags(country)) {
        if (name != target) {
            run_flag_effect(execute_slot(kClrCountryFlagVtable), name, scope, arg);
        }
    }
    if (!to_default) {
        run_flag_effect(execute_slot(kSetCountryFlagVtable), target, scope, arg);
    }
    std::lock_guard<std::mutex> lock(g_mutex());
    if (static_cast<size_t>(index) < g_countries().size()) {
        g_countries()[static_cast<size_t>(index)].known = false;
    }
}

std::once_flag g_texts_once;
std::map<std::string, std::string>& g_texts() { static std::map<std::string, std::string> v; return v; }

std::string text(const std::string& key) {
    std::call_once(g_texts_once, [] {
        g_texts() = game::read_localisation({"SE_CHANGE_FLAG_EFFECT", "SE_CHANGE_FLAG_DEFAULT_EFFECT"});
    });
    auto it = g_texts().find(key);
    return it == g_texts().end() ? key : it->second;
}

// Slot 9, the tooltip: thiscall (std::string* out, scope, std::string by value, int), RET 0x28. `out` is
// uninitialised (the result); the callee destroys the string it got by value.
uintptr_t __fastcall tooltip(uintptr_t effect, void*, uintptr_t out, uintptr_t, GameString by_value, int) {
    std::string value = GameString::copy(effect + kEffectName);
    std::string line;
    if (_stricmp(value.c_str(), kDefault) == 0) {
        line = text("SE_CHANGE_FLAG_DEFAULT_EFFECT");
    } else {
        line = text("SE_CHANGE_FLAG_EFFECT");
        size_t at = line.find("$FLAG$");
        if (at != std::string::npos) {
            std::string path = normalise(value);
            line.replace(at, 6, path.empty() ? value : path);
        }
    }
    std::memset(reinterpret_cast<void*>(out), 0, sizeof(GameString));
    *reinterpret_cast<uint32_t*>(out + 0x14) = 15;
    using AssignFn = void(__fastcall*)(uintptr_t, void*, const char*, size_t);
    reinterpret_cast<AssignFn>(hooks::exe_va(kStringAssign))(out, nullptr, line.c_str(), line.size());
    if (by_value.capacity > 15) {
        game::game_delete(const_cast<char*>(by_value.ptr));
    }
    return out;
}

bool build(uintptr_t block, uintptr_t parser, int token, uintptr_t& result) {
    if (!g_token || token != g_token) {
        return false;
    }
    uintptr_t effect = game::build_template(block, parser, kTokenSetCountryFlag, hooks::exe_va(kSetCountryFlagVtable), result);
    if (effect) {
        *reinterpret_cast<uintptr_t*>(effect) = g_vtable;
    } else {
        logf("change_flag: an effect could not be built");
    }
    return true;
}

// Replaces the CALL CBManager_Load (stdcall (manager, block)): our keyword is registered before the
// files that use it are read.
void __stdcall load_cbs(uintptr_t manager, uintptr_t block) {
    if (!g_token) {
        if (game::lexer_ready()) {
            g_token = game::add_token(kKeyword);
            logf("change_flag: effect %s 0x%X", kKeyword, g_token);
        } else {
            logf("change_flag: the lexer is not ready before cb_types.txt; no effect");
        }
    }
    reinterpret_cast<void(__stdcall*)(uintptr_t, uintptr_t)>(g_prev_load)(manager, block);
}

}  // namespace

void install_change_flag(const Config& config) {
    if (!config.change_flag) {
        logf("change_flag: disabled");
        return;
    }
    uintptr_t set_vtable = hooks::exe_va(kSetCountryFlagVtable);
    g_vtable = game::copy_effect_vtable(set_vtable, g_vtable_storage, reinterpret_cast<uintptr_t>(&tooltip),
                                        reinterpret_cast<uintptr_t>(&execute));

    uintptr_t site = hooks::exe_va(kFlagTexture);
    if (!hooks::expect_bytes(site, kFlagTextureEntry, sizeof(kFlagTextureEntry))) {
        logf("change_flag: site %08X not recognised, off", kFlagTexture);
        return;
    }
    if (!(g_flag_texture_trampoline = hooks::make_trampoline(site, sizeof(kFlagTextureEntry)))) {
        logf("change_flag: site %08X could not be copied, off", kFlagTexture);
        return;
    }
    if (!hooks::chain_call_logged("change_flag", kCbManagerLoadCall, kCbManagerLoad, reinterpret_cast<uintptr_t>(&load_cbs),
                                  g_prev_load)) {
        return;
    }
    if (!game::add_effect_builder(&build)) {
        logf("change_flag: no effect factory hook, off");
        return;
    }
    if (!hooks::jump_hook(site, kFlagTextureEntry, sizeof(kFlagTextureEntry), reinterpret_cast<uintptr_t>(&flag_texture_thunk))) {
        logf("change_flag: site %08X could not be hooked; the effect keeps the choice but flags do not change", kFlagTexture);
        return;
    }
    if (!hooks::chain_call_logged("change_flag", kFinalizeEconomyCall, kEconomyUpdateFull,
                                  reinterpret_cast<uintptr_t>(&finalize_thunk), g_prev_finalize)) {
        logf("change_flag: loaded saves keep the flags read before (until the country's flags change)");
    }
    logf("change_flag: active, change_flag = \"path\" draws gfx/flags/extra/path.tga for the country");
}

}  // namespace se::features
