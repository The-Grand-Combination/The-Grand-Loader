#include "game/console.h"

#include <cstring>

#include "game/objects.h"
#include "game/script.h"
#include "hooks/memory.h"
#include "log.h"

// The game's console commands are 60 static entries of 0x4C bytes at 0x00F09740 (CConsoleCommand). The
// console's ctor (CConsole_ctor, 0x00613380) collects pointers to them into a vector<CConsoleCommand*>
// at console+0x48 through Console_CollectCommands (0x00423990, EAX = the vector), called at 0x00613845.
// Console_Dispatch (0x00423B50) finds an entry by name or alias in that vector and calls its handler,
// cdecl (result*, const vector<std::string>* args) -> result*, where args are the words after the name
// and result is {u8 ok; std::string text at +4}; the caller copies the text and frees the result's.
//
// The call at 0x00613845 goes to collect(): the game's function first, then our entries are appended to
// the vector (new storage from the game's heap; the old one freed there). Nothing else changes.

namespace se::game {

namespace {

constexpr uint32_t kCollectCall = 0x00613845;  // CALL Console_CollectCommands in CConsole_ctor
constexpr uint32_t kCollect = 0x00423990;      // EAX = vector<CConsoleCommand*>*
constexpr uint32_t kStringAssign = 0x00409350;  // thiscall (const char*, length)

constexpr size_t kMaxCommands = 8;
constexpr size_t kMaxArgs = 10;

// CConsoleCommand, as the static entries of the game.
struct ConsoleCommand {
    uint8_t enabled;  // 0: "Command available only for developers."
    uint8_t hidden;   // 1: left out of the command list
    uint16_t pad;
    const char* name;
    int alias_count;
    const char* aliases[3];
    const char* description;
    uintptr_t handler;
    int arg_count;
    const char* args[kMaxArgs];
};
static_assert(sizeof(ConsoleCommand) == 0x4C, "the game's console entries are 0x4C bytes");

ConsoleCommand g_commands[kMaxCommands];
ConsoleHandler g_handlers[kMaxCommands];
size_t g_count = 0;
uintptr_t g_collect = 0;

// result: {u8 ok; std::string text at +4}, uninitialised.
void write_result(uintptr_t result, bool ok, const std::string& text) {
    *reinterpret_cast<uint8_t*>(result) = ok ? 1 : 0;
    uintptr_t s = result + 4;
    std::memset(reinterpret_cast<void*>(s), 0, 0x1C);
    *reinterpret_cast<uint32_t*>(s + 0x14) = 15;
    using AssignFn = void(__fastcall*)(uintptr_t, void*, const char*, size_t);
    reinterpret_cast<AssignFn>(hooks::exe_va(kStringAssign))(s, nullptr, text.c_str(), text.size());
}

template <size_t N>
uintptr_t __cdecl run(uintptr_t result, uintptr_t args) {
    std::vector<std::string> words;
    uintptr_t begin = read<uintptr_t>(args), end = read<uintptr_t>(args + 4);
    for (uintptr_t s = begin; s < end; s += 0x1C) {
        words.push_back(GameString::copy(s));
    }
    bool ok = true;
    std::string text = g_handlers[N](words, ok);
    write_result(result, ok, text);
    return result;
}

const uintptr_t kRunners[kMaxCommands] = {
    reinterpret_cast<uintptr_t>(&run<0>), reinterpret_cast<uintptr_t>(&run<1>),
    reinterpret_cast<uintptr_t>(&run<2>), reinterpret_cast<uintptr_t>(&run<3>),
    reinterpret_cast<uintptr_t>(&run<4>), reinterpret_cast<uintptr_t>(&run<5>),
    reinterpret_cast<uintptr_t>(&run<6>), reinterpret_cast<uintptr_t>(&run<7>),
};

void __stdcall append(uintptr_t vector) {
    uintptr_t* v = reinterpret_cast<uintptr_t*>(vector);
    size_t old = (v[1] - v[0]) / 4;
    auto* storage = static_cast<uintptr_t*>(game_new((old + g_count) * 4));
    if (!storage) {
        return;
    }
    if (old) {
        std::memcpy(storage, reinterpret_cast<void*>(v[0]), old * 4);
    }
    for (size_t i = 0; i < g_count; ++i) {
        storage[old + i] = reinterpret_cast<uintptr_t>(&g_commands[i]);
    }
    if (v[0]) {
        game_delete(reinterpret_cast<void*>(v[0]));
    }
    v[0] = reinterpret_cast<uintptr_t>(storage);
    v[1] = v[2] = v[0] + (old + g_count) * 4;
}

// Replaces the CALL at kCollectCall: EAX = the vector, kept by the game's function.
__declspec(naked) void collect() {
    __asm {
        push eax
        call g_collect
        call append  // stdcall (vector): the EAX pushed above
        ret
    }
}

bool install() {
    static bool tried = false, installed = false;
    if (!tried) {
        tried = true;
        installed = hooks::chain_call_logged("console", kCollectCall, kCollect, reinterpret_cast<uintptr_t>(&collect),
                                             g_collect);
    }
    return installed;
}

}  // namespace

bool add_console_command(const char* name, const char* description, std::initializer_list<const char*> arg_names,
                         ConsoleHandler handler) {
    if (g_count == kMaxCommands || arg_names.size() > kMaxArgs || !install()) {
        return false;
    }
    ConsoleCommand& c = g_commands[g_count];
    c.enabled = 1;
    c.name = name;
    c.aliases[0] = "";
    c.description = description;
    c.handler = kRunners[g_count];
    c.arg_count = static_cast<int>(arg_names.size());
    size_t i = 0;
    for (const char* a : arg_names) {
        c.args[i++] = a;
    }
    g_handlers[g_count++] = handler;
    return true;
}

}  // namespace se::game
