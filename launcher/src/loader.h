#pragma once

namespace tgl {

// Runs once from DllMain. Does nothing unless the host process is v2game.exe.
void loader_init();

}  // namespace tgl
