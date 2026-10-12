#pragma once

namespace cp {

// Called from the crash handler after the registers and stack have been logged; features use it to
// dump their own state (e.g. which hooked functions were active).
using CrashContextFn = void (*)();

// Logs a fatal exception (address, registers, exe return addresses on the stack) to the_grand_loader.log and
// lets the game's own handling continue.
void install_crash_log(CrashContextFn context);

}  // namespace cp
