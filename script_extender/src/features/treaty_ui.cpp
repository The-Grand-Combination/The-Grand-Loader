#include "features/treaty_ui.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "features/treaty.h"
#include "game/localisation.h"
#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// Treaty screens: docs/re/systems/treaties.md ("Screens"), diplomacy.md ("Diplomacy UI").
//
// - Loading, with the game's per-file sequence (FileReader, ParserBlock, (target+4)->vt[0xC]): the
//   plugin's interface/*.gfx at the end of the game's .gfx loop (0x00635D89), so the sprites get the same
//   later setup as the game's (parsed after it, a sprite has no texture at +0xD0 and drawing it
//   crashes); its interface/*.gui before the CALL Gui_Finalize at 0x0063690D.
// - Answers: the accepted / declined messages are keyed SE_TREATY_<THEY|WE|OTHER>_<ACCEPT|DECLINE> (the
//   action's name + the suffix CDiplomaticActionCommand_Execute adds). interface/se_messagetypes.txt adds
//   those types (copies of the PEACE_* ones) right after interface/messagetypes.txt is read, and their
//   texts (<type>_1.._6, _HEADER, _LOG, ...) go from our CSV into the game's localisation table, as
//   Localize does for a missing key (Localisation_GetOrInsert, then Localisation_AddVariant).
// - Treaty CBs: common/se_treaty_cbs.txt of the plugin is appended to the cb_types.txt the game would read
//   (the -mod= folder's, else the game's; game::game_file), into common/se_cb_types.generated.txt, which CBManager_Load then parses
//   (it clears the CB list first, so a second call cannot add to it). Their names and descriptions come
//   from our CSV into the game's localisation table, as the message texts do.
// - "Diplomatic Play": CMakeCBAction's slot 14 (vtable entry 0x00E3440C) returns a chooser built on the
//   vanilla "DefaultDialog" box instead: Agree opens the vanilla Justify-war dialog, Decline the editor.
// - The editor is a CEU3Dialog on se_treaty_dialog (interface/se_treaty.gui) with our vtable copy (slots
//   0 dtor, 1 per-frame refresh, 6 send, 7 cancel). Its rows are fixed buttons, paged: the terms
//   (money first, then the CB terms; click = remove) and the picker (Demand / Offer tabs, then money or a
//   treaty CB, then an amount or a province).

namespace se::features::treaty_ui {

namespace {

using game::GameString;
using game::read;

// GUI loading
constexpr uint32_t kGfxLoopEnd = 0x00635D89;  // after the last .gfx: MOV EDX,[EBP-0x40]; MOV ECX,[EDX+0x128]
constexpr uint8_t kGfxLoopEndBytes[] = {0x8B, 0x55, 0xC0, 0x8B, 0x8A, 0x28, 0x01, 0x00, 0x00};
constexpr uint32_t kGuiFinalizeCall = 0x0063690D;  // CALL Gui_Finalize; EDI = the application
constexpr uint32_t kMessageTypesCall = 0x0063C4AA;  // CALL MessageTypes_Read for interface/messagetypes.txt
constexpr uint32_t kMessageTypesRead = 0x006A4FA0;  // stdcall (manager, block); deletes the block when done
constexpr uint32_t kMessageManager = 0x006A4F40;    // () -> the message-type manager
constexpr uint32_t kLocalisation = 0x0131C99C;      // g_localisation
constexpr uint32_t kLocalisationTable = 0x48;
constexpr uint32_t kStringHashMapFind = 0x00A727A0;     // EAX = table, (const std::string*) -> pair or 0
constexpr uint32_t kLocalisationGetOrInsert = 0x009AD930;  // ESI = key, EDI = localisation -> value
constexpr uint32_t kLocalisationAddVariant = 0x009AB900;   // stdcall (value, const std::string* text, selector*)
constexpr uint32_t kGuiFinalize = 0x009A4190;
constexpr uint32_t kAppGfx = 0x11C;
constexpr uint32_t kAppGui = 0x120;
constexpr uint32_t kFileReaderCtor = 0x0098FFC0;   // thiscall (const std::string* path)
constexpr uint32_t kFileReaderSize = 0x114;
constexpr uint32_t kTokenReaderVtable = 0x00E37C3C;
constexpr uint32_t kParserBlockCtor = 0x009A0BF0;  // EDX = const std::string* name, (mem, reader, 1)
constexpr uint32_t kParserBlockSize = 0x360;
constexpr uint32_t kReadBlock = 0x0C;              // (target+4)->vt[0xC](block)

// Dialogs
constexpr uint32_t kDialogCtor = 0x00640680;      // ECX = gui owner, (this, const std::string* type)
constexpr uint32_t kDialogDtorBody = 0x00640880;  // ECX = this
constexpr uint32_t kDialogIsActive = 0x006409B0;  // slot 1 of the base
constexpr uint32_t kDialogVtable = 0x00E08694;    // CEU3Dialog_vftable, 18 slots
constexpr uint32_t kDialogSlots = 18;
constexpr uint32_t kDialogBaseSize = 0x68;
constexpr uint32_t kDialogWindow = 0x04;
constexpr uint32_t kDialogOwner = 0x08;
constexpr uint32_t kDialogClosed = 0x64;
constexpr uint32_t kDialogState = 0x68;  // ours: the C++ state
constexpr uint32_t kAddDialog = 0x006480E0;  // GuiOwner_AddDialog: ESI = owner, (dialog)
constexpr uint32_t kSetLeftShield = 0x00641630;   // ECX = window, EAX = &{tag, index}
constexpr uint32_t kSetRightShield = 0x006416A0;
constexpr uint32_t kGlueVtable = 0x00E086E0;      // CButtonObserverGlue<CEU3Dialog>
constexpr uint32_t kOwnerGui = 0x34;              // gui owner vt: the GUI
constexpr uint32_t kGuiTypes = 0x34;              // gui + 0x34: type table, vt[4](const std::string*) -> type
// Window finders: thiscall (const std::string*) -> element or 0
constexpr uint32_t kFindButton = 0x34;
constexpr uint32_t kFindTextBox = 0x3C;
constexpr uint32_t kFindIcon = 0x44;
constexpr uint32_t kCbSpriteIndex = 0x0C;  // the frame of GFX_diplo_wargoal_icons (as CGoalEntry shows it)
// Buttons
constexpr uint32_t kButtonShow = 0x34;
constexpr uint32_t kButtonHide = 0x38;
constexpr uint32_t kButtonEnable = 0xB8;
constexpr uint32_t kButtonDisable = 0xBC;
constexpr uint32_t kButtonSetText = 0xCC;  // (const std::string*, font 0)
constexpr uint32_t kButtonObservers = 0x54;  // vt[4] add, vt[8] remove
// Text boxes
constexpr uint32_t kTextBoxString = 0xDC;
constexpr uint32_t kTextBoxSprite = 0xA0;
constexpr uint32_t kStringAssignFrom = 0x00408ED0;  // thiscall (const std::string*, pos, count)
constexpr uint32_t kTextSpriteSetText = 0x00A5ECC0;  // ESI = sprite, (const std::string*, 0)
// "Justify war"
constexpr uint32_t kRefreshCanPerform = 0x0062B9FC;  // CDiplomacyView_RefreshActions: CALL CanPerform, ESI = action
constexpr uint32_t kCanPerform = 0x00911760;         // DiplomaticAction_CanPerform: ESI = action, (bool) -> AL
constexpr uint32_t kMakeCbVtable = 0x00E343D4;
constexpr uint32_t kMakeCbDialogSlot = 0x00E3440C;   // CMakeCBAction_vftable slot 14
constexpr uint32_t kMakeCbDialog = 0x00926D00;       // thiscall (owner, view) -> dialog
constexpr uint32_t kActionClone = 0x30;              // action vt: Clone
constexpr uint32_t kActionActorIndex = 0x0C;
constexpr uint32_t kActionRecipientIndex = 0x14;

// Countries, provinces
constexpr uint32_t kCountryTag = 0x1C;
constexpr uint32_t kCountryProvinces = 0x9D8;
constexpr uint32_t kProvinceState = 0x188;
constexpr uint32_t kStateGetName = 0x004CECE0;  // CState vt[0x20]: thiscall (std::string* out) -> out
constexpr uint32_t kStringDtor = 0x00408AD0;    // std_string_dtor, fastcall (std::string*)
constexpr uint32_t kCountryTreasury = 0xE78;
constexpr uint32_t kCbName = 0x14;
constexpr uint32_t kCountryManagerVa = 0x012587E4;  // g_countryManager: +0 country count, +4 CCountry*[]
constexpr int64_t kMoneyOne = 32768;

constexpr const char* kEditorType = "se_treaty_dialog";
constexpr const char* kChooserType = "DefaultDialog";
constexpr int kTermRows = 8;  // se_term_<i> in se_treaty.gui
constexpr int kPickRows = 6;  // se_pick_<i>
constexpr int kMaxRows = kTermRows > kPickRows ? kTermRows : kPickRows;
constexpr int kMoneyAmounts[] = {100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000};
constexpr uintptr_t kMoneyOption = 1;       // in the CB options: the money item
constexpr uint32_t kButtonSetFrame = 0x48;  // Button_SetFrame: the tabs show frame 2 when active

Config g_config;
uintptr_t g_gui_finalize = 0;
uintptr_t g_gfx_back = 0;  // kGfxLoopEnd + 9
uintptr_t g_message_types_read = 0;
uintptr_t g_can_perform = 0;
uintptr_t g_make_cb_vtable = 0;
uintptr_t g_editor_vtable_storage[kDialogSlots + 1];
uintptr_t g_chooser_vtable_storage[kDialogSlots + 1];

template <class T>
T fn(uint32_t va) {
    return reinterpret_cast<T>(hooks::exe_va(va));
}

template <class T>
T vt(uintptr_t object, uint32_t slot) {
    return read<T>(read<uintptr_t>(object) + slot);
}

void* game_new(size_t size) { return fn<void*(__cdecl*)(size_t)>(0x00AAE9AF)(size); }
void game_delete(void* p) { fn<void(__cdecl*)(void*)>(0x00AAE91B)(p); }

// --- Loading the plugin's .gfx / .gui ------------------------------------------------------------

// The plugin folder as a PhysFS path (the game folder is the PhysFS root), e.g. "mod/script_extender".
std::string plugin_physfs_dir() {
    std::wstring game = host().game_dir;
    std::wstring plugin = host().plugin_dir;
    std::wstring rel = plugin.size() > game.size() && _wcsnicmp(plugin.c_str(), game.c_str(), game.size()) == 0
                           ? plugin.substr(game.size())
                           : std::wstring(L"mod\\script_extender");
    while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/')) {
        rel.erase(rel.begin());
    }
    std::string out;
    for (wchar_t c : rel) {
        out += c == L'\\' ? '/' : static_cast<char>(c);
    }
    return out;
}

std::vector<std::string> files(const wchar_t* pattern) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW((std::wstring(host().plugin_dir) + L"\\interface\\" + pattern).c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }
    do {
        std::string name;
        for (const wchar_t* c = data.cFileName; *c; ++c) {
            name += static_cast<char>(*c);
        }
        out.push_back(name);
    } while (FindNextFileW(h, &data));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// A parser block over the file at `path` (a PhysFS path), as EU3App_LoadResources makes one; 0 on failure.
uintptr_t make_block(const std::string& path) {
    void* block_memory = game_new(kParserBlockSize);
    void* reader = game_new(kFileReaderSize);
    if (!block_memory || !reader) {
        return 0;
    }
    GameString game_path(path);
    using ReaderCtorFn = void(__fastcall*)(void*, void*, const GameString*);
    fn<ReaderCtorFn>(kFileReaderCtor)(reader, nullptr, &game_path);
    *reinterpret_cast<uintptr_t*>(reader) = hooks::exe_va(kTokenReaderVtable);
    std::string session = "UNNAMED_SESSION";
    GameString game_session(session);
    const GameString* session_ptr = &game_session;
    uintptr_t ctor = hooks::exe_va(kParserBlockCtor);
    uintptr_t block;
    __asm {
        push 1
        push reader
        push block_memory
        mov edx, session_ptr
        call ctor
        mov block, eax
    }
    return block;
}

// One file into `target` (the sprite database or the GUI), as EU3App_LoadResources does.
void parse_file(uintptr_t target, const std::string& path) {
    uintptr_t block = make_block(path);
    if (!block) {
        return;
    }
    uintptr_t reader_iface = target + 4;
    using ReadFn = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    vt<ReadFn>(reader_iface, kReadBlock)(reader_iface, nullptr, block);
    using DeleteFn = void(__fastcall*)(uintptr_t, void*, int);
    vt<DeleteFn>(block, 0)(block, nullptr, 1);
    logf("treaty: loaded %s", path.c_str());
}

// --- Treaty answer messages ----------------------------------------------------------------------

const char* const kMessageTypes[] = {"SE_TREATY_THEY_ACCEPT", "SE_TREATY_WE_ACCEPT", "SE_TREATY_OTHER_ACCEPT",
                                     "SE_TREATY_THEY_DECLINE", "SE_TREATY_WE_DECLINE", "SE_TREATY_OTHER_DECLINE"};
const char* const kMessageSuffixes[] = {"_HEADER", "_1", "_2", "_3", "_4", "_5", "_6", "_LOG",
                                        "_BTN1", "_BTN2", "_BTN3", "_MAP", "_SETUP"};

// Replaces the CALL MessageTypes_Read of interface/messagetypes.txt: reads ours after it.
int __stdcall read_message_types(uintptr_t manager, uintptr_t block) {
    using ReadFn = int(__stdcall*)(uintptr_t, uintptr_t);
    int result = reinterpret_cast<ReadFn>(g_message_types_read)(manager, block);
    std::string path = plugin_physfs_dir() + "/interface/se_messagetypes.txt";
    if (uintptr_t ours = make_block(path)) {
        uintptr_t mgr = fn<uintptr_t(__cdecl*)()>(kMessageManager)();
        int r = reinterpret_cast<ReadFn>(g_message_types_read)(mgr, ours);
        logf("treaty: loaded %s (%d)", path.c_str(), r);
    }
    return result;
}

uintptr_t localisation_find(uintptr_t localisation, const std::string& key) {
    GameString game_key(key);
    const GameString* key_ptr = &game_key;
    uintptr_t table = localisation + kLocalisationTable;
    uintptr_t find = hooks::exe_va(kStringHashMapFind);
    uintptr_t pair;
    __asm {
        mov eax, table
        push key_ptr
        call find
        mov pair, eax
    }
    return pair;
}

// CBs with `po_demand_province = yes` (demand_province): their terms take one province, so the editor
// lists provinces for them and states for the others. Read from the cb_types.txt text CBManager_Load gets,
// since after loading such a CB has the same peace options as a po_demand_state one.
std::vector<std::string> g_province_cbs;

void find_province_cbs(const std::string& text) {
    g_province_cbs.clear();
    std::vector<std::string> words;
    std::string word;
    bool comment = false;
    for (char c : text + "\n") {
        if (comment) {
            comment = c != '\n';
            continue;
        }
        bool separator = c == '#' || c == '=' || c == '{' || c == '}' || static_cast<unsigned char>(c) <= ' ';
        if (!separator) {
            word += c;
            continue;
        }
        if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
        comment = c == '#';
        if (c == '=' || c == '{' || c == '}') {
            words.push_back(std::string(1, c));
        }
    }
    // each CB is a top-level `name = { ... }` block
    int depth = 0;
    std::string cb;
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i] == "{") {
            if (depth == 0 && i >= 2 && words[i - 1] == "=") {
                cb = words[i - 2];
            }
            ++depth;
        } else if (words[i] == "}") {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (depth == 1 && words[i] == "po_demand_province" && i + 2 < words.size() && words[i + 1] == "=" &&
                   words[i + 2] == "yes") {
            g_province_cbs.push_back(cb);
        }
    }
}

}  // namespace

bool takes_one_province(uintptr_t cb) {
    std::string name = GameString::copy(cb + kCbName);
    return std::find(g_province_cbs.begin(), g_province_cbs.end(), name) != g_province_cbs.end();
}

// The texts of the answer messages and of the treaty CBs (name, _desc, _setup, _short), from our CSV into
// the game's table; keys the game already has stay. Cheap when they are all there; called before anything
// can show one.
void ensure_message_texts() {
    uintptr_t localisation = read<uintptr_t>(hooks::exe_va(kLocalisation));
    if (!localisation) {
        return;
    }
    std::vector<std::string> keys;
    for (const char* type : kMessageTypes) {
        for (const char* suffix : kMessageSuffixes) {
            keys.push_back(std::string(type) + suffix);
        }
    }
    for (uintptr_t cb : treaty::treaty_cbs()) {
        std::string name = GameString::copy(cb + kCbName);
        for (const char* suffix : {"", "_desc", "_setup", "_short"}) {
            keys.push_back(name + suffix);
        }
    }
    std::vector<std::string> missing;
    for (const std::string& key : keys) {
        if (!localisation_find(localisation, key)) {
            missing.push_back(key);
        }
    }
    if (missing.empty()) {
        return;
    }
    std::map<std::string, std::string> texts = game::read_localisation(missing);
    uintptr_t get_or_insert = hooks::exe_va(kLocalisationGetOrInsert);
    uintptr_t add_variant = hooks::exe_va(kLocalisationAddVariant);
    int added = 0;
    for (const auto& [key, text] : texts) {
        GameString game_key(key);
        const GameString* key_ptr = &game_key;
        uintptr_t value;
        __asm {
            push esi
            push edi
            mov esi, key_ptr
            mov edi, localisation
            call get_or_insert
            mov value, eax
            pop edi
            pop esi
        }
        GameString game_text(text);
        uint32_t selector[4] = {0, 0, 0, 0};  // what Localize passes for a key it inserts
        using AddFn = void(__stdcall*)(uintptr_t, const GameString*, const uint32_t*);
        reinterpret_cast<AddFn>(add_variant)(value, &game_text, selector);
        ++added;
    }
    logf("treaty: %d texts added to the localisation", added);
}

uintptr_t merged_cb_block() {
    std::wstring plugin = host().plugin_dir;
    std::ifstream ours_in(plugin + L"\\common\\se_treaty_cbs.txt", std::ios::binary);
    if (!ours_in) {
        logf("treaty: no common/se_treaty_cbs.txt in the plugin folder; only the mod's treaty CBs");
        return 0;
    }
    std::string ours((std::istreambuf_iterator<char>(ours_in)), std::istreambuf_iterator<char>());
    // The cb_types.txt the game reads: the mod's (from -mod= on the command line), else the game's.
    std::wstring windows_source = game::game_file(L"common\\cb_types.txt");
    std::string source;
    for (wchar_t c : windows_source) {
        source += static_cast<char>(c);
    }
    std::ifstream game_in(windows_source, std::ios::binary);
    if (windows_source.empty() || !game_in) {
        logf("treaty: no cb_types.txt found; treaty CBs of the plugin not loaded");
        return 0;
    }
    std::string game((std::istreambuf_iterator<char>(game_in)), std::istreambuf_iterator<char>());
    find_province_cbs(game + "\n" + ours);
    logf("treaty: %u CBs with po_demand_province (listed by province)", static_cast<unsigned>(g_province_cbs.size()));
    std::ofstream out(plugin + L"\\common\\se_cb_types.generated.txt", std::ios::binary | std::ios::trunc);
    out << game << "\r\n\r\n# --- Script Extender: common/se_treaty_cbs.txt ---\r\n" << ours << "\r\n";
    out.close();
    if (!out) {
        logf("treaty: could not write common/se_cb_types.generated.txt; treaty CBs of the plugin not loaded");
        return 0;
    }
    std::string merged = plugin_physfs_dir() + "/common/se_cb_types.generated.txt";
    logf("treaty: CBs from %s + common/se_treaty_cbs.txt", source.c_str());
    return make_block(merged);
}

namespace {

void __stdcall load_gfx(uintptr_t app) {
    std::string dir = plugin_physfs_dir() + "/interface/";
    if (uintptr_t gfx = read<uintptr_t>(app + kAppGfx)) {
        for (const std::string& name : files(L"*.gfx")) {
            parse_file(gfx, dir + name);
        }
    }
}

// Jumped to from kGfxLoopEnd (EBP = EU3App_LoadResources' frame, [EBP-0x40] = the application).
__declspec(naked) void gfx_thunk() {
    __asm {
        SE_SAVE_ALL
        push dword ptr [ebp - 0x40]
        call load_gfx
        SE_RESTORE_ALL
        mov edx, [ebp - 0x40]
        mov ecx, [edx + 0x128]
        jmp g_gfx_back
    }
}

void __stdcall load_files(uintptr_t app) {
    ensure_message_texts();
    std::string dir = plugin_physfs_dir() + "/interface/";
    uintptr_t gui = read<uintptr_t>(app + kAppGui);
    if (gui) {
        for (const std::string& name : files(L"*.gui")) {
            parse_file(gui, dir + name);
        }
    }
}

__declspec(naked) void finalize_thunk() {
    __asm {
        SE_SAVE_ALL
        push edi
        call load_files
        SE_RESTORE_ALL
        jmp g_gui_finalize
    }
}

// --- GUI helpers -----------------------------------------------------------------------------------

uintptr_t find(uintptr_t window, uint32_t finder, const std::string& name) {
    GameString game_name(name);
    using FindFn = uintptr_t(__fastcall*)(uintptr_t, void*, const GameString*);
    return vt<FindFn>(window, finder)(window, nullptr, &game_name);
}

void set_text(uintptr_t textbox, const std::string& text) {
    if (!textbox) {
        return;
    }
    GameString value(text);
    using AssignFn = void(__fastcall*)(uintptr_t, void*, const GameString*, size_t, size_t);
    fn<AssignFn>(kStringAssignFrom)(textbox + kTextBoxString, nullptr, &value, 0, static_cast<size_t>(-1));
    uintptr_t sprite = read<uintptr_t>(textbox + kTextBoxSprite);
    const GameString* value_ptr = &value;
    uintptr_t set = hooks::exe_va(kTextSpriteSetText);
    __asm {
        push esi
        mov esi, sprite
        push 0
        push value_ptr
        call set
        pop esi
    }
}

void button_text(uintptr_t button, const std::string& text) {
    if (!button) {
        return;
    }
    GameString value(text);
    using TextFn = void(__fastcall*)(uintptr_t, void*, const GameString*, int);
    vt<TextFn>(button, kButtonSetText)(button, nullptr, &value, 0);
}

void button_call(uintptr_t button, uint32_t slot) {
    if (button) {
        vt<void(__fastcall*)(uintptr_t, void*)>(button, slot)(button, nullptr);
    }
}

void show(uintptr_t button, bool visible) { button_call(button, visible ? kButtonShow : kButtonHide); }

// A CB's icon on `icon` (an iconType of GFX_diplo_wargoal_icons), or hidden for cb 0.
void cb_icon(uintptr_t icon, uintptr_t cb) {
    show(icon, cb != 0);
    if (icon && cb) {
        vt<void(__fastcall*)(uintptr_t, void*, int)>(icon, kButtonSetFrame)(icon, nullptr, read<int>(cb + kCbSpriteIndex));
    }
}
void enable(uintptr_t button, bool on) { button_call(button, on ? kButtonEnable : kButtonDisable); }

void shield(uintptr_t window, uint32_t setter, int country) {
    uint32_t ref[2] = {read<uint32_t>(game::country(country) + kCountryTag), static_cast<uint32_t>(country)};
    uint32_t* ref_ptr = ref;
    uintptr_t f = hooks::exe_va(setter);
    __asm {
        mov ecx, window
        mov eax, ref_ptr
        call f
    }
}

// The button-observer glue of the game (0x2C bytes): slot 1 calls [+0x20] with ECX = owner, (button).
struct Glue {
    uintptr_t vtable;
    uintptr_t owner;
    uintptr_t handlers[9];
};
static_assert(sizeof(Glue) == 0x2C, "glue");
constexpr int kGlueClickWithButton = 6;  // +0x20

void observe(uintptr_t button, Glue* glue, bool add) {
    if (!button) {
        return;
    }
    uintptr_t list = button + kButtonObservers;
    using ObserveFn = void(__fastcall*)(uintptr_t, void*, Glue*);
    vt<ObserveFn>(list, add ? 4 : 8)(list, nullptr, glue);
}

bool gui_type_exists(uintptr_t owner, const char* type) {
    uintptr_t gui = vt<uintptr_t(__fastcall*)(uintptr_t, void*)>(owner, kOwnerGui)(owner, nullptr);
    uintptr_t types = gui + kGuiTypes;
    std::string name = type;
    GameString game_name(name);
    using LookupFn = uintptr_t(__fastcall*)(uintptr_t, void*, const GameString*);
    return vt<LookupFn>(types, 4)(types, nullptr, &game_name) != 0;
}

// A CEU3Dialog of `type` with `vtable`, `extra` bytes after the base; 0 if the type is unknown.
uintptr_t new_dialog(uintptr_t owner, const char* type, uintptr_t* vtable_storage, size_t extra) {
    if (!gui_type_exists(owner, type)) {
        logf("treaty: the window type %s is not loaded", type);
        return 0;
    }
    void* memory = game_new(kDialogBaseSize + extra);
    if (!memory) {
        return 0;
    }
    std::memset(static_cast<uint8_t*>(memory) + kDialogBaseSize, 0, extra);
    std::string name = type;
    GameString game_name(name);
    using CtorFn = uintptr_t(__fastcall*)(uintptr_t, void*, void*, const GameString*);
    fn<CtorFn>(kDialogCtor)(owner, nullptr, memory, &game_name);
    auto dialog = reinterpret_cast<uintptr_t>(memory);
    *reinterpret_cast<uintptr_t*>(dialog) = reinterpret_cast<uintptr_t>(&vtable_storage[1]);
    return dialog;
}

void add_dialog(uintptr_t owner, uintptr_t dialog) {
    vt<void(__fastcall*)(uintptr_t, void*)>(dialog, 0x28)(dialog, nullptr);
    uintptr_t f = hooks::exe_va(kAddDialog);
    __asm {
        push esi
        mov esi, owner
        push dialog
        call f
        pop esi
    }
}

void destroy_dialog_base(uintptr_t dialog, int flags) {
    using DtorFn = void(__fastcall*)(uintptr_t, void*);
    fn<DtorFn>(kDialogDtorBody)(dialog, nullptr);
    if (flags & 1) {
        game_delete(reinterpret_cast<void*>(dialog));
    }
}

void delete_action(uintptr_t action) {
    if (action) {
        vt<void(__fastcall*)(uintptr_t, void*, int)>(action, 0)(action, nullptr, 1);
    }
}

// --- Texts -----------------------------------------------------------------------------------------

const char* const kUiKeys[] = {
    "SE_TREATY_TITLE",        "SE_TREATY_TERMS",       "SE_TREATY_MODE_DEMAND",    "SE_TREATY_MODE_GIVE",
    "SE_TREATY_PICK_CB_DEMAND", "SE_TREATY_PICK_CB_OFFER", "SE_TREATY_PICK_PROVINCE", "SE_TREATY_NO_OPTIONS",
    "SE_TREATY_BACK",         "SE_TREATY_MONEY",       "SE_TREATY_PICK_MONEY",     "SE_TREATY_SCORE",
    "SE_TREATY_WILL_ACCEPT",  "SE_TREATY_WILL_NOT_ACCEPT", "SE_TREATY_SEND",       "SE_TREATY_CANCEL",
    "SE_TREATY_TERM_GAIN",    "SE_TREATY_TERM_GIVE",   "SE_TREATY_CHOOSER_TITLE", "SE_TREATY_CHOOSER_DESC",
    "SE_TREATY_CHOOSER_JUSTIFY", "SE_TREATY_CHOOSER_TREATY", "SE_TREATY_PICK_COUNTRY",
};

std::string replace(std::string s, const std::string& token, const std::string& value) {
    for (size_t at = s.find(token); at != std::string::npos; at = s.find(token, at + value.size())) {
        s.replace(at, token.size(), value);
    }
    return s;
}

// --- The editor --------------------------------------------------------------------------------------

enum Mode { kDemand, kGive };  // kDemand: they give, we gain

struct Editor {
    uintptr_t dialog = 0;
    uintptr_t owner = 0;
    uintptr_t action = 0;
    int me = 0;
    int them = 0;
    Glue glue{};
    std::map<std::string, std::string> texts;
    // children
    uintptr_t window = 0;
    uintptr_t terms[kMaxRows] = {};
    uintptr_t picks[kMaxRows] = {};
    uintptr_t term_icons[kMaxRows] = {};
    uintptr_t pick_icons[kMaxRows] = {};
    uintptr_t term_prev = 0, term_next = 0, term_page_text = 0, mode_demand_text = 0, mode_give_text = 0;
    uintptr_t mode_demand = 0, mode_give = 0, prev = 0, next = 0, back = 0, agree = 0, decline = 0;
    uintptr_t title = 0, terms_label = 0, pick_label = 0, score = 0, acceptance = 0;
    std::vector<uintptr_t> observed;
    // picker
    Mode mode = kDemand;
    uintptr_t picked_cb = 0;  // 0: choosing a CB; kMoneyOption: choosing an amount
    int picked_third = 0;     // the third country chosen for picked_cb (CBs with allowed_countries)
    std::vector<int> country_options;
    int page = 0;
    int term_page = 0;
    std::vector<uintptr_t> cb_options;
    std::vector<uint32_t> province_options;  // or amounts in pounds
    bool dirty = true;

    std::string t(const char* key) const {
        auto it = texts.find(key);
        return it == texts.end() ? std::string(key) : it->second;
    }
    int gains() const { return mode == kDemand ? me : them; }
    int gives() const { return mode == kDemand ? them : me; }
};

Editor* editor_of(uintptr_t dialog) { return read<Editor*>(dialog + kDialogState); }

std::string name_of(const Editor& e, const std::string& key) {
    auto it = e.texts.find(key);
    return it == e.texts.end() ? key : it->second;
}

std::string cb_name(const Editor& e, uintptr_t cb) { return name_of(e, GameString::copy(cb + kCbName)); }

std::string province_name(const Editor& e, uint32_t id) {
    char key[16];
    std::snprintf(key, sizeof(key), "PROV%u", id);
    return name_of(e, key);
}

// The name of the province's state, as the war goal dialog shows it (CState_GetName).
std::string state_name(uint32_t province) {
    uintptr_t state = read<uintptr_t>(game::province(province) + kProvinceState);
    if (!state) {
        return {};
    }
    alignas(4) uint8_t out[sizeof(GameString)];
    using NameFn = void*(__thiscall*)(uintptr_t, void*);
    fn<NameFn>(kStateGetName)(state, out);
    std::string name = GameString::copy(reinterpret_cast<uintptr_t>(out));
    fn<void(__fastcall*)(void*)>(kStringDtor)(out);
    return name;
}

// What a pick or term of `cb` at `province` is called: the province for po_demand_province CBs, else
// the state the term takes.
std::string place_name(const Editor& e, uintptr_t cb, uint32_t province) {
    return takes_one_province(cb) ? province_name(e, province) : state_name(province);
}

std::string country_name(const Editor& e, int index) {
    uint32_t tag = read<uint32_t>(game::country(index) + kCountryTag);
    return name_of(e, std::string(reinterpret_cast<const char*>(&tag), 3));
}

void compute_options(Editor& e) {
    e.cb_options.clear();
    e.province_options.clear();
    e.country_options.clear();
    if (!e.picked_cb) {
        e.cb_options.push_back(kMoneyOption);
        for (uintptr_t cb : treaty::treaty_cbs()) {
            if (treaty::cb_unsupported(cb) || !treaty::cb_usable(cb, e.gains(), e.gives())) {
                logf("treaty: %s not offered (%d gains from %d): %s", GameString::copy(cb + kCbName).c_str(), e.gains(),
                     e.gives(), treaty::cb_unsupported(cb) ? "unsupported peace option" : "can_use fails");
                continue;
            }
            if (!treaty::cb_needs_province(cb) && !treaty::cb_needs_country(cb) &&
                !treaty::check_term(e.action, {cb, e.gains(), e.gives(), 0}).empty()) {
                continue;
            }
            e.cb_options.push_back(cb);
        }
        return;
    }
    if (e.picked_cb == kMoneyOption) {
        int64_t m = treaty::money(e.action);
        for (int amount : kMoneyAmounts) {
            int64_t next = m + (e.mode == kGive ? amount : -amount) * kMoneyOne;  // positive: we pay
            uintptr_t payer = game::country(next > 0 ? e.me : e.them);
            if (next == 0 || read<int64_t>(payer + kCountryTreasury) >= (next > 0 ? next : -next)) {
                e.province_options.push_back(static_cast<uint32_t>(amount));
            }
        }
        return;
    }
    if (treaty::cb_needs_country(e.picked_cb) && !e.picked_third) {
        int count = read<int>(read<uintptr_t>(hooks::exe_va(kCountryManagerVa)));
        int allowed = 0;
        std::string first_problem;
        for (int c = 1; c < count; ++c) {
            if (c == e.me || c == e.them) {
                continue;
            }
            if (!treaty::country_allowed(e.picked_cb, e.gains(), e.gives(), c)) {
                continue;
            }
            ++allowed;
            // with a province still to choose, the whole term is checked then
            std::string why = treaty::cb_needs_province(e.picked_cb)
                                  ? std::string()
                                  : treaty::check_term(e.action, {e.picked_cb, e.gains(), e.gives(), 0, c});
            if (why.empty()) {
                e.country_options.push_back(c);
            } else if (first_problem.empty()) {
                first_problem = why;
            }
        }
        // Diagnostics: every country with cores in the giver's provinces, and what allowed_countries says.
        {
            uintptr_t giver = game::country(e.gives());
            std::map<int, int> cores;  // country -> provinces of the giver with its core
            for (const int* p = read<const int*>(giver + kCountryProvinces); p < read<const int*>(giver + kCountryProvinces + 4); ++p) {
                uintptr_t province = game::province(static_cast<uint32_t>(*p));
                for (uintptr_t c = read<uintptr_t>(province + 0x148); c < read<uintptr_t>(province + 0x14C); c += 8) {
                    int idx = read<int>(c + 4);
                    if (idx != e.gives()) {
                        ++cores[idx];
                    }
                }
            }
            for (const auto& [idx, n] : cores) {
                uint32_t tag = read<uint32_t>(game::country(idx) + kCountryTag);
                logf("treaty:   core holder %.3s (%d) on %d provinces: allowed_countries %s", reinterpret_cast<const char*>(&tag),
                     idx, n, treaty::country_allowed(e.picked_cb, e.gains(), e.gives(), idx) ? "yes" : "no");
                static int detailed = 0;
                if (detailed < 3) {
                    ++detailed;
                    treaty::log_country_conditions(e.picked_cb, e.gains(), e.gives(), idx);
                }
            }
            // and the giver's sphere: its member list (+0x1418) and every country naming it leader (+0x142C)
            std::map<int, int> sphere;  // country -> 1 in the list, 2 names it, 3 both
            for (uintptr_t m = read<uintptr_t>(giver + 0x1418); m < read<uintptr_t>(giver + 0x141C); m += 8) {
                sphere[read<int>(m + 4)] |= 1;
            }
            for (int c = 1; c < count; ++c) {
                if (c != e.gives() && read<int>(game::country(c) + 0x142C) == e.gives()) {
                    sphere[c] |= 2;
                }
            }
            logf("treaty:   %d sphere members of the giver", static_cast<int>(sphere.size()));
            for (const auto& [idx, how] : sphere) {
                uint32_t tag = read<uint32_t>(game::country(idx) + kCountryTag);
                logf("treaty:   sphere member %.3s (%d) [%s]: allowed_countries %s", reinterpret_cast<const char*>(&tag), idx,
                     how == 3 ? "list+leader" : how == 1 ? "list only" : "leader only",
                     treaty::country_allowed(e.picked_cb, e.gains(), e.gives(), idx) ? "yes" : "no");
                treaty::log_country_conditions(e.picked_cb, e.gains(), e.gives(), idx);
            }
        }
        logf("treaty: %s, %d gains from %d: %d of %d countries pass allowed_countries, %u usable%s%s",
             GameString::copy(e.picked_cb + kCbName).c_str(), e.gains(), e.gives(), allowed, count - 1,
             static_cast<unsigned>(e.country_options.size()), first_problem.empty() ? "" : "; first refusal: ",
             first_problem.c_str());
        return;
    }
    if (!treaty::cb_needs_province(e.picked_cb)) {
        return;
    }
    uintptr_t giver = game::country(e.gives());
    auto begin = read<const int*>(giver + kCountryProvinces);
    auto end = read<const int*>(giver + kCountryProvinces + 4);
    std::map<std::string, int> refusals;
    bool per_state = !takes_one_province(e.picked_cb);
    std::vector<uintptr_t> listed_states;
    for (const int* p = begin; p < end; ++p) {
        treaty::Term term{e.picked_cb, e.gains(), e.gives(), static_cast<uint32_t>(*p), e.picked_third};
        uintptr_t state = read<uintptr_t>(game::province(term.province) + kProvinceState);
        if (per_state && std::find(listed_states.begin(), listed_states.end(), state) != listed_states.end()) {
            continue;  // the term takes the whole state: one row for it
        }
        std::string why = treaty::check_term(e.action, term);
        if (why.empty()) {
            e.province_options.push_back(term.province);
            listed_states.push_back(state);
        } else {
            ++refusals[why];
        }
    }
    if (e.picked_third) {
        std::string summary;
        for (const auto& [why, n] : refusals) {
            summary += " [" + std::to_string(n) + "x " + why + "]";
        }
        logf("treaty: %s for country %d: %u of %u provinces%s", GameString::copy(e.picked_cb + kCbName).c_str(),
             e.picked_third, static_cast<unsigned>(e.province_options.size()), static_cast<unsigned>(end - begin),
             summary.c_str());
    }
}

bool choosing_country(const Editor& e) {
    return e.picked_cb && e.picked_cb != kMoneyOption && treaty::cb_needs_country(e.picked_cb) && !e.picked_third;
}

size_t option_count(const Editor& e) {
    return !e.picked_cb ? e.cb_options.size() : choosing_country(e) ? e.country_options.size() : e.province_options.size();
}

std::string option_name(const Editor& e, uintptr_t cb) { return cb == kMoneyOption ? e.t("SE_TREATY_MONEY") : cb_name(e, cb); }

std::string pounds_of(int64_t fixed) { return std::to_string((fixed < 0 ? -fixed : fixed) / kMoneyOne); }

void refresh(Editor& e) {
    e.dirty = false;
    set_text(e.title, e.t("SE_TREATY_TITLE"));
    set_text(e.terms_label, e.t("SE_TREATY_TERMS"));
    set_text(e.mode_demand_text, e.t("SE_TREATY_MODE_DEMAND"));
    set_text(e.mode_give_text, e.t("SE_TREATY_MODE_GIVE"));
    using FrameFn = void(__fastcall*)(uintptr_t, void*, int);
    if (e.mode_demand && e.mode_give) {
        vt<FrameFn>(e.mode_demand, kButtonSetFrame)(e.mode_demand, nullptr, e.mode == kDemand ? 2 : 1);
        vt<FrameFn>(e.mode_give, kButtonSetFrame)(e.mode_give, nullptr, e.mode == kGive ? 2 : 1);
    }
    button_text(e.back, e.t("SE_TREATY_BACK"));
    button_text(e.agree, e.t("SE_TREATY_SEND"));
    button_text(e.decline, e.t("SE_TREATY_CANCEL"));

    // Terms
    std::vector<treaty::Term> terms = treaty::terms(e.action);
    int gain = 0, give = 0;
    for (const treaty::Term& term : terms) {
        (term.actor == e.me ? gain : give) += treaty::term_cost(term);
    }
    int64_t m = treaty::money(e.action);
    size_t money_rows = m != 0 ? 1 : 0;
    size_t rows = money_rows + terms.size();
    int term_pages = std::max(1, static_cast<int>((rows + kTermRows - 1) / kTermRows));
    e.term_page = std::max(0, std::min(e.term_page, term_pages - 1));
    show(e.term_prev, e.term_page > 0);
    show(e.term_next, e.term_page + 1 < term_pages);
    set_text(e.term_page_text, term_pages > 1 ? std::to_string(e.term_page + 1) + "/" + std::to_string(term_pages) : "");
    for (int row = 0; row < kTermRows; ++row) {
        int i = e.term_page * kTermRows + row;
        if (i >= static_cast<int>(rows)) {
            show(e.terms[row], false);
            cb_icon(e.term_icons[row], 0);
            continue;
        }
        if (static_cast<size_t>(i) < money_rows) {
            std::string label = e.t("SE_TREATY_MONEY") + " " + pounds_of(m);
            show(e.terms[row], true);
            cb_icon(e.term_icons[row], 0);
            button_text(e.terms[row], replace(e.t(m < 0 ? "SE_TREATY_TERM_GAIN" : "SE_TREATY_TERM_GIVE"), "$TERM$", label));
            continue;
        }
        const treaty::Term& term = terms[i - money_rows];
        std::string label = cb_name(e, term.cb);
        if (term.third) {
            label += " " + country_name(e, term.third);
        }
        if (term.province) {
            label += " " + place_name(e, term.cb, term.province);
        }
        char cost[16];
        std::snprintf(cost, sizeof(cost), " (%d)", treaty::term_cost(term));
        label = replace(e.t(term.actor == e.me ? "SE_TREATY_TERM_GAIN" : "SE_TREATY_TERM_GIVE"), "$TERM$", label + cost);
        show(e.terms[row], true);
        cb_icon(e.term_icons[row], term.cb);
        button_text(e.terms[row], label);
    }

    // Picker
    size_t count = option_count(e);
    int pages = static_cast<int>((count + kPickRows - 1) / kPickRows);
    e.page = std::max(0, std::min(e.page, pages - 1));
    std::string pick_label = e.picked_cb == kMoneyOption ? e.t("SE_TREATY_PICK_MONEY")
                             : choosing_country(e) ? replace(e.t("SE_TREATY_PICK_COUNTRY"), "$CB$", cb_name(e, e.picked_cb))
                             : e.picked_cb ? replace(e.t("SE_TREATY_PICK_PROVINCE"), "$CB$", cb_name(e, e.picked_cb))
                             : e.t(e.mode == kDemand ? "SE_TREATY_PICK_CB_DEMAND" : "SE_TREATY_PICK_CB_OFFER");
    if (count == 0) {
        pick_label = e.t("SE_TREATY_NO_OPTIONS");
    }
    set_text(e.pick_label, pick_label);
    for (int i = 0; i < kPickRows; ++i) {
        size_t index = static_cast<size_t>(e.page * kPickRows + i);
        bool visible = index < count;
        show(e.picks[i], visible);
        uintptr_t row_cb = visible && !e.picked_cb && e.cb_options[index] != kMoneyOption ? e.cb_options[index] : 0;
        cb_icon(e.pick_icons[i], row_cb);
        if (visible) {
            button_text(e.picks[i], e.picked_cb == kMoneyOption ? std::to_string(e.province_options[index])
                                    : choosing_country(e)     ? country_name(e, e.country_options[index])
                                    : e.picked_cb             ? place_name(e, e.picked_cb, e.province_options[index])
                                                              : option_name(e, e.cb_options[index]));
        }
    }
    show(e.prev, e.page > 0);
    show(e.next, e.page + 1 < pages);
    show(e.back, e.picked_cb != 0);

    // Summary
    std::string score = e.t("SE_TREATY_SCORE");
    score = replace(replace(score, "$GAIN$", std::to_string(gain)), "$GIVE$", std::to_string(give));
    set_text(e.score, score);
    std::string why = treaty::problem(e.action);
    std::string acceptance = why;
    if (why.empty() && !treaty::is_human(e.them)) {
        acceptance = treaty::ai_accepts(e.action, e.them) ? e.t("SE_TREATY_WILL_ACCEPT") : e.t("SE_TREATY_WILL_NOT_ACCEPT");
    }
    set_text(e.acceptance, replace(acceptance, "$COUNTRY$", country_name(e, e.them)));
    enable(e.agree, why.empty() && treaty::can_send(e.action));
}

void reset_picker(Editor& e) {
    e.page = 0;
    compute_options(e);
    e.dirty = true;
}

void __fastcall on_click(uintptr_t dialog, void*, uintptr_t button) {
    Editor* e = editor_of(dialog);
    if (!e) {
        return;
    }
    for (int i = 0; i < kMaxRows; ++i) {
        if (button == e->terms[i]) {
            size_t money_rows = treaty::money(e->action) != 0 ? 1 : 0;
            size_t row = static_cast<size_t>(e->term_page * kTermRows + i);
            if (row < money_rows) {
                treaty::set_money(e->action, 0);
            } else {
                treaty::remove_term(e->action, row - money_rows);
            }
            reset_picker(*e);
            return;
        }
        if (button == e->picks[i]) {
            size_t index = static_cast<size_t>(e->page * kPickRows + i);
            if (choosing_country(*e) && index < e->country_options.size()) {
                e->picked_third = e->country_options[index];
                if (!treaty::cb_needs_province(e->picked_cb)) {
                    treaty::add_term(e->action, {e->picked_cb, e->gains(), e->gives(), 0, e->picked_third});
                    e->term_page = 1 << 20;  // show the last page, where the new term is
                    e->picked_cb = 0;
                    e->picked_third = 0;
                }
            } else if (e->picked_cb == kMoneyOption && index < e->province_options.size()) {
                int64_t amount = static_cast<int64_t>(e->province_options[index]) * kMoneyOne;
                treaty::set_money(e->action, treaty::money(e->action) + (e->mode == kGive ? amount : -amount));
                e->picked_cb = 0;
                e->picked_third = 0;
            } else if (!e->picked_cb && index < e->cb_options.size()) {
                uintptr_t cb = e->cb_options[index];
                if (cb == kMoneyOption || treaty::cb_needs_province(cb) || treaty::cb_needs_country(cb)) {
                    e->picked_cb = cb;
                    e->picked_third = 0;
                } else {
                    treaty::add_term(e->action, {cb, e->gains(), e->gives(), 0});
                    e->term_page = 1 << 20;  // show the last page, where the new term is
                }
            } else if (e->picked_cb && index < e->province_options.size()) {
                treaty::add_term(e->action, {e->picked_cb, e->gains(), e->gives(), e->province_options[index], e->picked_third});
                e->term_page = 1 << 20;  // show the last page, where the new term is
                e->picked_cb = 0;
                e->picked_third = 0;
            }
            reset_picker(*e);
            return;
        }
    }
    if (button == e->mode_demand || button == e->mode_give) {
        e->mode = button == e->mode_demand ? kDemand : kGive;
        e->picked_cb = 0;
        e->picked_third = 0;
        reset_picker(*e);
    } else if (button == e->back) {
        e->picked_cb = 0;
        e->picked_third = 0;
        reset_picker(*e);
    } else if (button == e->term_prev || button == e->term_next) {
        e->term_page += button == e->term_next ? 1 : -1;
        e->dirty = true;
    } else if (button == e->prev || button == e->next) {
        e->page += button == e->next ? 1 : -1;
        e->dirty = true;
    }
}

uintptr_t __fastcall editor_dtor(uintptr_t dialog, void*, int flags) {
    if (Editor* e = editor_of(dialog)) {
        for (uintptr_t b : e->observed) {
            observe(b, &e->glue, false);
        }
        delete_action(e->action);
        delete e;
        *reinterpret_cast<Editor**>(dialog + kDialogState) = nullptr;
    }
    destroy_dialog_base(dialog, flags);
    return dialog;
}

bool __fastcall editor_update(uintptr_t dialog, void*) {
    Editor* e = editor_of(dialog);
    if (e && e->dirty) {
        refresh(*e);
    }
    return fn<bool(__fastcall*)(uintptr_t, void*)>(kDialogIsActive)(dialog, nullptr);
}

void __fastcall editor_agree(uintptr_t dialog, void*) {
    Editor* e = editor_of(dialog);
    if (e && treaty::can_send(e->action)) {
        treaty::send(e->action, e->owner);
    }
    *reinterpret_cast<uint8_t*>(dialog + kDialogClosed) = 1;
}

void __fastcall dialog_close(uintptr_t dialog, void*) { *reinterpret_cast<uint8_t*>(dialog + kDialogClosed) = 1; }

void open_editor(uintptr_t owner, int me, int them) {
    ensure_message_texts();
    uintptr_t action = treaty::create(me, them);
    if (!action) {
        return;
    }
    uintptr_t dialog = new_dialog(owner, kEditorType, g_editor_vtable_storage, sizeof(Editor*));
    if (!dialog) {
        delete_action(action);
        return;
    }
    auto* e = new Editor;
    *reinterpret_cast<Editor**>(dialog + kDialogState) = e;
    e->dialog = dialog;
    e->owner = owner;
    e->action = action;
    e->me = me;
    e->them = them;
    e->glue.vtable = hooks::exe_va(kGlueVtable);
    e->glue.owner = dialog;
    e->glue.handlers[kGlueClickWithButton] = reinterpret_cast<uintptr_t>(&on_click);

    // Names shown in the editor: our texts, the treaty CBs, both countries and their provinces.
    std::vector<std::string> keys(std::begin(kUiKeys), std::end(kUiKeys));
    for (uintptr_t cb : treaty::treaty_cbs()) {
        keys.push_back(GameString::copy(cb + kCbName));
    }
    int country_count = read<int>(read<uintptr_t>(hooks::exe_va(kCountryManagerVa)));
    for (int c = 1; c < country_count; ++c) {
        uint32_t tag = read<uint32_t>(game::country(c) + kCountryTag);
        keys.emplace_back(reinterpret_cast<const char*>(&tag), 3);
    }
    for (int c : {me, them}) {
        uintptr_t country = game::country(c);
        for (const int* p = read<const int*>(country + kCountryProvinces); p < read<const int*>(country + kCountryProvinces + 4); ++p) {
            keys.push_back("PROV" + std::to_string(*p));
        }
    }
    e->texts = game::read_localisation(keys);

    uintptr_t w = read<uintptr_t>(dialog + kDialogWindow);
    e->window = w;
    auto button = [&](const std::string& name) {
        uintptr_t b = find(w, kFindButton, name);
        if (b) {
            observe(b, &e->glue, true);
            e->observed.push_back(b);
        } else {
            logf("treaty: %s has no button %s", kEditorType, name.c_str());
        }
        return b;
    };
    for (int i = 0; i < kMaxRows; ++i) {
        if (i < kTermRows) {
            e->terms[i] = button("se_term_" + std::to_string(i));
            e->term_icons[i] = find(w, kFindIcon, "se_term_icon_" + std::to_string(i));
        }
        if (i < kPickRows) {
            e->picks[i] = button("se_pick_" + std::to_string(i));
            e->pick_icons[i] = find(w, kFindIcon, "se_pick_icon_" + std::to_string(i));
        }
    }
    e->term_prev = button("se_term_prev");
    e->term_next = button("se_term_next");
    e->term_page_text = find(w, kFindTextBox, "se_term_page");
    e->mode_demand_text = find(w, kFindTextBox, "se_mode_demand_text");
    e->mode_give_text = find(w, kFindTextBox, "se_mode_give_text");
    e->mode_demand = button("se_mode_demand");
    e->mode_give = button("se_mode_give");
    e->prev = button("se_pick_prev");
    e->next = button("se_pick_next");
    e->back = button("se_pick_back");
    e->agree = find(w, kFindButton, "AgreeButton");
    e->decline = find(w, kFindButton, "DeclineButton");
    e->title = find(w, kFindTextBox, "Title");
    e->terms_label = find(w, kFindTextBox, "se_terms_label");
    e->pick_label = find(w, kFindTextBox, "se_pick_label");
    e->score = find(w, kFindTextBox, "score");
    e->acceptance = find(w, kFindTextBox, "acceptance");
    shield(w, kSetLeftShield, me);
    shield(w, kSetRightShield, them);
    reset_picker(*e);
    refresh(*e);
    add_dialog(owner, dialog);
    logf("treaty: editor %d -> %d (%u treaty CBs)", me, them, static_cast<unsigned>(treaty::treaty_cbs().size()));
}

// The vanilla gate of an action (DiplomaticAction_CanPerform), e.g. whether a CB may be justified.
bool vanilla_can_perform(uintptr_t action) {
    uintptr_t f = g_can_perform;
    bool result;
    __asm {
        push esi
        mov esi, action
        push 1
        call f
        mov result, al
        pop esi
    }
    return result;
}

// The button's state: "Diplomatic Play" is usable when a treaty is, whatever the CB rules say (those
// still decide "Justify Casus Belli" in the chooser and the command that fabricates).
bool __stdcall button_usable(uintptr_t action) {
    if (treaty::enabled() &&
        treaty::possible(read<int>(action + kActionActorIndex), read<int>(action + kActionRecipientIndex))) {
        return true;
    }
    return vanilla_can_perform(action);
}

// Replaces the CALL CanPerform of CDiplomacyView_RefreshActions; ESI = the button's action, (bool).
__declspec(naked) void refresh_thunk() {
    __asm {
        mov eax, [esi]
        cmp eax, g_make_cb_vtable
        jne vanilla
        SE_SAVE_ALL
        push esi
        call button_usable
        movzx eax, al
        mov [esp + SE_SAVED_EAX], eax
        SE_RESTORE_ALL
        ret 4
    vanilla:
        jmp g_can_perform
    }
}

// --- "Diplomatic Play" chooser ------------------------------------------------------------------------

constexpr uint32_t kChooserAction = 0x68;  // the CMakeCBAction clone
constexpr uint32_t kChooserView = 0x6C;
constexpr size_t kChooserExtra = 8;

uintptr_t __fastcall chooser_dtor(uintptr_t dialog, void*, int flags) {
    delete_action(read<uintptr_t>(dialog + kChooserAction));
    *reinterpret_cast<uintptr_t*>(dialog + kChooserAction) = 0;
    destroy_dialog_base(dialog, flags);
    return dialog;
}

// Agree: the vanilla Justify-war dialog.
void __fastcall chooser_justify(uintptr_t dialog, void*) {
    uintptr_t owner = read<uintptr_t>(dialog + kDialogOwner);
    uintptr_t action = read<uintptr_t>(dialog + kChooserAction);
    uintptr_t view = read<uintptr_t>(dialog + kChooserView);
    using CreateFn = uintptr_t(__fastcall*)(uintptr_t, void*, uintptr_t, uintptr_t);
    uintptr_t vanilla = fn<CreateFn>(kMakeCbDialog)(action, nullptr, owner, view);
    if (vanilla) {
        add_dialog(owner, vanilla);
    }
    *reinterpret_cast<uint8_t*>(dialog + kDialogClosed) = 1;
}

// Decline: the treaty editor.
void __fastcall chooser_treaty(uintptr_t dialog, void*) {
    uintptr_t owner = read<uintptr_t>(dialog + kDialogOwner);
    uintptr_t action = read<uintptr_t>(dialog + kChooserAction);
    *reinterpret_cast<uint8_t*>(dialog + kDialogClosed) = 1;
    open_editor(owner, read<int>(action + kActionActorIndex), read<int>(action + kActionRecipientIndex));
}

// Replaces CMakeCBAction slot 14: thiscall (owner, view) -> dialog.
uintptr_t __fastcall make_cb_dialog(uintptr_t action, void*, uintptr_t owner, uintptr_t view) {
    using CreateFn = uintptr_t(__fastcall*)(uintptr_t, void*, uintptr_t, uintptr_t);
    auto vanilla = fn<CreateFn>(kMakeCbDialog);
    if (!treaty::enabled()) {
        return vanilla(action, nullptr, owner, view);
    }
    uintptr_t dialog = new_dialog(owner, kChooserType, g_chooser_vtable_storage, kChooserExtra);
    if (!dialog) {
        return vanilla(action, nullptr, owner, view);
    }
    *reinterpret_cast<uintptr_t*>(dialog + kChooserAction) =
        vt<uintptr_t(__fastcall*)(uintptr_t, void*)>(action, kActionClone)(action, nullptr);
    *reinterpret_cast<uintptr_t*>(dialog + kChooserView) = view;
    std::vector<std::string> keys = {"SE_TREATY_CHOOSER_TITLE", "SE_TREATY_CHOOSER_DESC", "SE_TREATY_CHOOSER_JUSTIFY",
                                     "SE_TREATY_CHOOSER_TREATY"};
    auto texts = game::read_localisation(keys);
    auto t = [&](const std::string& key) { return texts.count(key) ? texts[key] : key; };
    uintptr_t w = read<uintptr_t>(dialog + kDialogWindow);
    set_text(find(w, kFindTextBox, "Title"), t("SE_TREATY_CHOOSER_TITLE"));
    set_text(find(w, kFindTextBox, "Description"), t("SE_TREATY_CHOOSER_DESC"));
    uintptr_t justify = find(w, kFindButton, "AgreeButton");
    button_text(justify, t("SE_TREATY_CHOOSER_JUSTIFY"));
    enable(justify, vanilla_can_perform(action));  // the CB rules (truce, relations, ...) still apply here
    button_text(find(w, kFindButton, "DeclineButton"), t("SE_TREATY_CHOOSER_TREATY"));
    shield(w, kSetLeftShield, read<int>(action + kActionActorIndex));
    shield(w, kSetRightShield, read<int>(action + kActionRecipientIndex));
    return dialog;
}

void build_vtable(uintptr_t* storage, uintptr_t dtor, uintptr_t update, uintptr_t agree, uintptr_t decline) {
    uintptr_t base = hooks::exe_va(kDialogVtable);
    storage[0] = read<uintptr_t>(base - 4);
    for (uint32_t i = 0; i < kDialogSlots; ++i) {
        storage[i + 1] = read<uintptr_t>(base + i * 4);
    }
    storage[0 + 1] = dtor;
    if (update) {
        storage[1 + 1] = update;
    }
    storage[6 + 1] = agree;
    storage[7 + 1] = decline;
}

}  // namespace

bool install(const Config& config) {
    g_config = config;
    build_vtable(g_editor_vtable_storage, reinterpret_cast<uintptr_t>(&editor_dtor),
                 reinterpret_cast<uintptr_t>(&editor_update), reinterpret_cast<uintptr_t>(&editor_agree),
                 reinterpret_cast<uintptr_t>(&dialog_close));
    build_vtable(g_chooser_vtable_storage, reinterpret_cast<uintptr_t>(&chooser_dtor), 0,
                 reinterpret_cast<uintptr_t>(&chooser_justify), reinterpret_cast<uintptr_t>(&chooser_treaty));
    g_gui_finalize = hooks::exe_va(kGuiFinalize);
    g_message_types_read = hooks::exe_va(kMessageTypesRead);
    if (!hooks::redirect_call(hooks::exe_va(kMessageTypesCall), g_message_types_read,
                              reinterpret_cast<uintptr_t>(&read_message_types))) {
        logf("treaty: the message types call at %08X is not the expected one; no answer messages", kMessageTypesCall);
    }
    g_gfx_back = hooks::exe_va(kGfxLoopEnd) + sizeof(kGfxLoopEndBytes);
    if (!hooks::jump_hook(hooks::exe_va(kGfxLoopEnd), kGfxLoopEndBytes, sizeof(kGfxLoopEndBytes),
                          reinterpret_cast<uintptr_t>(&gfx_thunk))) {
        logf("treaty: the end of the .gfx loop at %08X is not the expected one", kGfxLoopEnd);
        return false;
    }
    g_can_perform = hooks::exe_va(kCanPerform);
    g_make_cb_vtable = hooks::exe_va(kMakeCbVtable);
    if (!hooks::redirect_call(hooks::exe_va(kRefreshCanPerform), g_can_perform, reinterpret_cast<uintptr_t>(&refresh_thunk))) {
        logf("treaty: the diplomacy buttons' CanPerform call at %08X is not the expected one", kRefreshCanPerform);
        return false;
    }
    if (!hooks::redirect_call(hooks::exe_va(kGuiFinalizeCall), g_gui_finalize, reinterpret_cast<uintptr_t>(&finalize_thunk))) {
        logf("treaty: the Gui_Finalize call at %08X is not the expected one", kGuiFinalizeCall);
        return false;
    }
    if (!hooks::replace_pointer(hooks::exe_va(kMakeCbDialogSlot), hooks::exe_va(kMakeCbDialog),
                                reinterpret_cast<uintptr_t>(&make_cb_dialog))) {
        logf("treaty: CMakeCBAction's dialog slot at %08X is not the expected one", kMakeCbDialogSlot);
        return false;
    }
    return true;
}

}  // namespace se::features::treaty_ui
