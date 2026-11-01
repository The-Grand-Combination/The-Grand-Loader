#pragma once

#include <initializer_list>
#include <string>
#include <vector>

// Console commands of our own, listed with the game's. Layouts: docs/re/systems/ui-console.md.

namespace se::game {

// Runs a command: `args` are the words after its name. Returns the line the console prints; `ok` false
// prints it as an error.
using ConsoleHandler = std::string (*)(const std::vector<std::string>& args, bool& ok);

// Adds a command to every console the game builds from now on (at most 8; `arg_names` at most 10, for the
// game's usage line). The strings must be static. False when the console hook could not be installed.
bool add_console_command(const char* name, const char* description, std::initializer_list<const char*> arg_names,
                         ConsoleHandler handler);

}  // namespace se::game
