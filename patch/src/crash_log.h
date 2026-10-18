#pragma once

namespace cp {

// Called from the crash handler after the registers and stack have been logged; features use it to
// dump their own state (e.g. which hooked functions were active).
using CrashContextFn = void (*)();

// Logs fatal exceptions (address, registers, exe return addresses on the stack) and the first C++ throws
// to the_grand_loader.log, and lets the game's own handling continue.
void install_crash_log();

// Adds a function the crash handler calls after its own lines. At most four.
void add_crash_context(CrashContextFn context);

}  // namespace cp
