#pragma once

namespace v2cp {

// Appends `suffix` to the version string the game shows in the main-menu corner ("V2 v3.04 (XXXX)").
// Returns false when the expected code is not at the documented site (different build).
bool patch_version_label(void* exe_base, const char* suffix);

}  // namespace v2cp
