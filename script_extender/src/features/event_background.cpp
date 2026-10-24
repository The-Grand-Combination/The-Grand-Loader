#include "features/event_background.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "game/objects.h"
#include "hooks/memory.h"
#include "log.h"

// Events and their window: docs/re/systems/events.md ("Event window", "A background command").
//
// - CEvent's vtable slot 4 (Event_OnToken, 0x008A6280) gets a wrapper. For the key `background` (token
//   0x99, which the original rejects as unexpected) it keeps the value per event; every other key goes
//   to the original.
// - Both calls of EventWindow_ctor (Event_Fire and the console `event` command) go through a thunk that
//   runs the constructor and then, for an event with a background, does what the constructor does for
//   the picture: finds the file (the mod's first, as SearchPaths_PickExisting), makes a TexturedSprite of
//   it and gives it to the window's `Background` element (SetSprite, vt slot 0xE0; the element owns and
//   deletes it). Display only: nothing is saved or sent.

namespace se::features {

namespace {

using game::GameString;
using game::read;

constexpr uint32_t kEventOnTokenSlot = 0x00E2C844;  // CEvent_vftable slot 4
constexpr uint32_t kEventOnToken = 0x008A6280;      // thiscall (event; parser, key token), RET 8
constexpr uint32_t kWindowCtor = 0x008A8F30;        // stdcall (this, event, CEventScope by value), RET 0x50
constexpr uint32_t kWindowCtorCalls[] = {
    0x008A8476,  // Event_Fire
    0x0041F54E,  // Console_Event
};
constexpr uint32_t kSpriteTypesFind = 0x005C2AD0;     // EAX = table, (const std::string* name), RET 4
constexpr uint32_t kTexturedSpriteCtor = 0x009C4B50;  // stdcall (this, size, path, template, device), RET 0x14
constexpr uint32_t kTexturedSpriteSize = 0x1AC;
constexpr uint32_t kOperatorNew = 0x00AAE9AF;   // cdecl (size)
constexpr uint32_t kPhysfsExists = 0x00B21820;  // cdecl (const char* path) -> nonzero when found
constexpr uint32_t kRenderDevice = 0x01258A74;  // g_renderDevice
constexpr uint32_t kSearchPaths = 0x012586E0;   // g_searchPaths

// g_renderDevice: the sprite-type table, and the two values the picture code packs as the sprite size
constexpr uint32_t kSpriteTypes = 0xA0;
constexpr uint32_t kDeviceSizeLow = 0x6B9BC;
constexpr uint32_t kDeviceSizeHigh = 0x6B9C0;
// g_searchPaths: folder i of the game at +0x84 + i * 0x1C, of the mod at +0x244 + i * 0x1C (std::string);
// +0x4C = 1 mod only, 2 mod then game, else game. Folder 2 is the one the picture code takes ("gfx").
constexpr uint32_t kSearchMode = 0x4C;
constexpr uint32_t kGameFolders = 0x84;
constexpr uint32_t kModFolders = 0x244;
constexpr uint32_t kFolderSize = 0x1C;
constexpr uint32_t kGfxFolder = 2;
constexpr const char* kBackgroundFolder = "/pictures/events/backgrounds/";
constexpr const char* kExtensions[] = {".dds", ".tga"};

constexpr int kTokenBackground = 0x99;
constexpr uint32_t kParserValueText = 0x22C;  // NUL-terminated value of the key just read
constexpr uint32_t kEventWindowWindow = 0x04;
constexpr uint32_t kEventWindowEvent = 0x0C;
constexpr uint32_t kFindChild = 0x44;  // window vt: thiscall (const std::string* name) -> element or 0
constexpr uint32_t kSetSprite = 0xE0;  // element vt: thiscall (sprite)
constexpr const char* kBackgroundChild = "Background";
// Template of the new sprite (effect and settings; the texture comes from the file). The four event
// backgrounds of eventwindow.gfx differ only in the texture.
constexpr const char* kTemplates[] = {"GFX_event_country_bg", "GFX_event_no_image"};

std::mutex g_mutex;                              // events may be read off the main thread
std::map<uintptr_t, std::string> g_backgrounds;  // event -> file name without folder and extension
std::set<std::string> g_reported;                // missing files already logged
uintptr_t g_window_ctor = 0;                     // EventWindow_ctor, resolved
uintptr_t g_ctor_return = 0;                     // caller of the thunk; the constructor is not reentrant

using OnTokenFn = void(__fastcall*)(uintptr_t event, void*, uintptr_t parser, int token);

void __fastcall on_token(uintptr_t event, void*, uintptr_t parser, int token) {
    if (token != kTokenBackground) {
        reinterpret_cast<OnTokenFn>(hooks::exe_va(kEventOnToken))(event, nullptr, parser, token);
        return;
    }
    const char* value = reinterpret_cast<const char*>(parser + kParserValueText);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_backgrounds[event] = value;
}

template <class T>
T vt(uintptr_t object, uint32_t slot) {
    return read<T>(read<uintptr_t>(object) + slot);
}

// The listing VA of a vtable, for the log.
uint32_t listing_va(uintptr_t runtime) {
    return static_cast<uint32_t>(runtime - hooks::exe_base() + hooks::kListingBase);
}

bool physfs_exists(const std::string& path) {
    using ExistsFn = int(__cdecl*)(const char*);
    return reinterpret_cast<ExistsFn>(hooks::exe_va(kPhysfsExists))(path.c_str()) != 0;
}

// The file of `name` as the game finds a picture: in the mod's gfx folder, then in the game's; .dds
// before .tga. Empty when there is none.
std::string find_file(const std::string& name) {
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
            std::string path = root + kBackgroundFolder + name + ext;
            if (physfs_exists(path)) {
                return path;
            }
        }
    }
    return {};
}

uintptr_t find_child(uintptr_t window, const GameString* name) {
    uintptr_t fn = vt<uintptr_t>(window, kFindChild);
    uintptr_t child;
    __asm {
        mov eax, name
        push eax
        mov ecx, window
        call fn
        mov child, eax
    }
    return child;
}

uintptr_t find_sprite_type(uintptr_t device, const GameString* name) {
    uintptr_t table = device + kSpriteTypes;
    uintptr_t fn = hooks::exe_va(kSpriteTypesFind);
    uintptr_t found;  // `type` is reserved in inline asm
    __asm {
        push name
        mov eax, table
        call fn
        mov found, eax
    }
    return found;
}

// A TexturedSprite of the file at `path`, as the picture code makes it; 0 on failure.
uintptr_t make_sprite(const std::string& path) {
    uintptr_t device = read<uintptr_t>(hooks::exe_va(kRenderDevice));
    if (!device) {
        return 0;
    }
    uintptr_t tmpl = 0;
    for (const char* name : kTemplates) {
        const std::string text = name;
        GameString game_name(text);
        tmpl = find_sprite_type(device, &game_name);
        if (tmpl) {
            break;
        }
    }
    if (!tmpl) {
        logf("event_background: none of the template sprite types is declared");
        return 0;
    }
    void* memory = reinterpret_cast<void*(__cdecl*)(size_t)>(hooks::exe_va(kOperatorNew))(kTexturedSpriteSize);
    if (!memory) {
        return 0;
    }
    uint32_t size = (read<uint32_t>(device + kDeviceSizeLow) & 0xFFFF) |
                    (read<uint32_t>(device + kDeviceSizeHigh) << 16);
    GameString game_path(path);  // the constructor copies it
    using CtorFn = uintptr_t(__stdcall*)(void*, uint32_t, const GameString*, uintptr_t, uintptr_t);
    return reinterpret_cast<CtorFn>(hooks::exe_va(kTexturedSpriteCtor))(memory, size, &game_path, tmpl, device);
}

void __stdcall after_window(uintptr_t event_window) {
    if (!event_window) {
        return;
    }
    uintptr_t event = read<uintptr_t>(event_window + kEventWindowEvent);
    std::string name;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_backgrounds.find(event);
        if (it == g_backgrounds.end()) {
            return;
        }
        name = it->second;
    }
    uintptr_t window = read<uintptr_t>(event_window + kEventWindowWindow);
    if (!window) {
        return;
    }
    std::string path = find_file(name);
    if (path.empty()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_reported.insert(name).second) {
            logf("event_background: no gfx%s%s.dds or .tga (mod or game)", kBackgroundFolder, name.c_str());
        }
        return;
    }
    const std::string background = kBackgroundChild;
    GameString child_name(background);
    uintptr_t child = find_child(window, &child_name);
    if (!child) {
        logf("event_background: the event window has no '%s' element; %s not shown", kBackgroundChild,
             path.c_str());
        return;
    }
    uintptr_t sprite = make_sprite(path);
    if (!sprite) {
        return;
    }
    using SetSpriteFn = void(__fastcall*)(uintptr_t self, void*, uintptr_t sprite);
    vt<SetSpriteFn>(child, kSetSprite)(child, nullptr, sprite);
    logf("event_background: %s (Background element vtable %08X)", path.c_str(), listing_va(read<uintptr_t>(child)));
}

// Replaces CALL EventWindow_ctor: the arguments stay where the caller pushed them (the scope is a
// by-value object that points to itself, so it must not be copied); the constructor pops them.
__declspec(naked) void window_ctor_thunk() {
    __asm {
        pop g_ctor_return
        call g_window_ctor
        push eax
        push eax
        call after_window
        pop eax
        push g_ctor_return
        ret
    }
}

}  // namespace

void install_event_background(const Config& config) {
    if (!config.event_background) {
        return;
    }
    g_window_ctor = hooks::exe_va(kWindowCtor);
    if (!hooks::replace_pointer(hooks::exe_va(kEventOnTokenSlot), hooks::exe_va(kEventOnToken),
                                reinterpret_cast<uintptr_t>(&on_token))) {
        logf("event_background: CEvent vtable slot at %08X is not the expected one, off", kEventOnTokenSlot);
        return;
    }
    int calls = 0;
    for (uint32_t site : kWindowCtorCalls) {
        if (hooks::redirect_call(hooks::exe_va(site), g_window_ctor, reinterpret_cast<uintptr_t>(&window_ctor_thunk))) {
            ++calls;
        } else {
            logf("event_background: the EventWindow_ctor call at %08X is not the expected one", site);
        }
    }
    logf("event_background: on (%d of 2 window calls)", calls);
}

}  // namespace se::features
