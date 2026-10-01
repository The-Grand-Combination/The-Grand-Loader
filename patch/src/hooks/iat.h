#pragma once

#include <windows.h>

namespace cp::hooks {

// Replaces `module`'s import of `function` from `dll` with `replacement`.
// Returns the original pointer, or nullptr if the import was not found.
void* hook_iat(HMODULE module, const char* dll, const char* function, void* replacement);

}  // namespace cp::hooks
