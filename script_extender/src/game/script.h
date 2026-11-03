#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reading script files with the game's own parser. Layouts: docs/re/systems/events.md ("Body keywords"),
// buildings.md ("buildings_ai.txt").

namespace se::game {

// Parser fields: the key, the `=` and the value just read, each a 0x104-byte token record {type, text}.
constexpr uint32_t kParserKeyToken = 0x20;
constexpr uint32_t kParserKeyText = 0x24;     // NUL-terminated
constexpr uint32_t kParserValueType = 0x228;  // 3 = `{`
constexpr uint32_t kParserValueText = 0x22C;  // NUL-terminated
constexpr int kTokenOpenBrace = 3;

void* game_new(size_t size);  // the game's operator new: for objects the game may delete
void game_delete(void* p);    // the game's operator delete: for memory from game_new or the game

// A parser block over the file at `path` (a path from the game folder, which is the PhysFS root), as
// EU3App_LoadResources makes one; 0 on failure. Delete it with delete_parser_block.
uintptr_t parser_block(const std::string& path);
void delete_parser_block(uintptr_t block);

// `absolute` as a path from the game folder with '/' separators (what parser_block takes); empty when it
// is outside the game folder.
std::string game_relative(const std::wstring& absolute);

// Reads the block the parser is in (to its `}` or the end of the file) into `object`, a game persistent
// object (its vtable slot 4 gets every `key = value`): Persistent_ReadBlock.
void read_block(uintptr_t object, uintptr_t parser);

// A persistent object of our own for read_block: on_key gets every `key = value` of the block, with the
// parser positioned after the value (a `{` value must be read, by read_block or skip_value, before
// returning).
class ScriptBlock {
public:
    virtual void on_key(const std::string& key, uintptr_t parser) = 0;
    void read(uintptr_t parser);

protected:
    ~ScriptBlock() = default;
};

// Reads and ignores the value just read when it is a `{` block.
void skip_value(uintptr_t parser);

// The value just read, as text.
std::string value_text(uintptr_t parser);
bool value_is_block(uintptr_t parser);

// Plain reading of a script file without the game's parser: the whole file, and its words (names, numbers,
// `=`, `{`, `}`; comments dropped).
std::string read_file(const std::wstring& path);
std::vector<std::string> script_words(const std::string& text);

}  // namespace se::game
