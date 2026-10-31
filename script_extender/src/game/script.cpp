#include "game/script.h"

#include <windows.h>

#include "game/objects.h"
#include "hooks/memory.h"
#include "log.h"

namespace se::game {

namespace {

constexpr uint32_t kOperatorNew = 0x00AAE9AF;      // cdecl (size)
constexpr uint32_t kOperatorDelete = 0x00AAE91B;   // cdecl (p)
constexpr uint32_t kFileReaderCtor = 0x0098FFC0;   // thiscall (const std::string* path)
constexpr uint32_t kFileReaderSize = 0x114;
constexpr uint32_t kTokenReaderVtable = 0x00E37C3C;
constexpr uint32_t kParserBlockCtor = 0x009A0BF0;  // EDX = const std::string* name, (mem, reader, 1)
constexpr uint32_t kParserBlockSize = 0x360;
constexpr uint32_t kReadBlock = 0x009A2A90;  // Persistent_ReadBlock: thiscall (parser); calls slots 4 and 5
constexpr uint32_t kEmptyMethod = 0x004010C0;  // RET: slot 5 of objects without an end-of-block step

// The object read_block sees: our vtable, then the C++ reader. Persistent_ReadBlock calls only slot 4
// (`key = value`) and slot 5 (after a clean block).
struct ScriptBlockObject {
    const uintptr_t* vtable;
    ScriptBlock* owner;
};

// Slot 4: thiscall (parser, key token), RET 8.
void __fastcall on_token(ScriptBlockObject* self, void*, uintptr_t parser, int) {
    self->owner->on_key(reinterpret_cast<const char*>(parser + kParserKeyText), parser);
}

void __fastcall no_op(void*, void*) {}

const uintptr_t* script_vtable() {
    static uintptr_t table[6];
    if (!table[3]) {
        table[0] = reinterpret_cast<uintptr_t>(&no_op);  // never destroyed through the vtable
        table[1] = reinterpret_cast<uintptr_t>(&no_op);
        table[2] = reinterpret_cast<uintptr_t>(&no_op);
        table[3] = hooks::exe_va(kReadBlock);
        table[4] = reinterpret_cast<uintptr_t>(&on_token);
        table[5] = hooks::exe_va(kEmptyMethod);
    }
    return table;
}

// Ignores every key; a nested block is read the same way.
class SkipBlock final : public ScriptBlock {
public:
    void on_key(const std::string&, uintptr_t parser) override { skip_value(parser); }
};

}  // namespace

void* game_new(size_t size) {
    return reinterpret_cast<void*(__cdecl*)(size_t)>(hooks::exe_va(kOperatorNew))(size);
}

void game_delete(void* p) { reinterpret_cast<void(__cdecl*)(void*)>(hooks::exe_va(kOperatorDelete))(p); }

uintptr_t parser_block(const std::string& path) {
    void* block_memory = game_new(kParserBlockSize);
    if (!block_memory) {
        return 0;
    }
    void* reader = game_new(kFileReaderSize);
    if (!reader) {
        game_delete(block_memory);  // nothing was constructed in it yet
        return 0;
    }
    GameString game_path(path);
    using ReaderCtorFn = void(__fastcall*)(void*, void*, const GameString*);
    reinterpret_cast<ReaderCtorFn>(hooks::exe_va(kFileReaderCtor))(reader, nullptr, &game_path);
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

void delete_parser_block(uintptr_t block) {
    using DeleteFn = void(__fastcall*)(uintptr_t, void*, int);
    read<DeleteFn>(read<uintptr_t>(block))(block, nullptr, 1);
}

std::string game_relative(const std::wstring& absolute) {
    std::wstring game = host().game_dir;
    if (absolute.size() <= game.size() || _wcsnicmp(absolute.c_str(), game.c_str(), game.size()) != 0) {
        return std::string();
    }
    std::wstring rel = absolute.substr(game.size());
    while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/')) {
        rel.erase(rel.begin());
    }
    std::string out;
    for (wchar_t c : rel) {
        out += c == L'\\' ? '/' : static_cast<char>(c);
    }
    return out;
}

void read_block(uintptr_t object, uintptr_t parser) {
    using ReadFn = void(__fastcall*)(uintptr_t, void*, uintptr_t);
    reinterpret_cast<ReadFn>(hooks::exe_va(kReadBlock))(object, nullptr, parser);
}

void ScriptBlock::read(uintptr_t parser) {
    ScriptBlockObject object{script_vtable(), this};
    read_block(reinterpret_cast<uintptr_t>(&object), parser);
}

bool value_is_block(uintptr_t parser) { return read<int>(parser + kParserValueType) == kTokenOpenBrace; }

void skip_value(uintptr_t parser) {
    if (value_is_block(parser)) {
        SkipBlock skip;
        skip.read(parser);
    }
}

std::string value_text(uintptr_t parser) { return reinterpret_cast<const char*>(parser + kParserValueText); }

}  // namespace se::game
