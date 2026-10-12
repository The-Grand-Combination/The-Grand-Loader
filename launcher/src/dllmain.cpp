#include <windows.h>

#include "loader.h"

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        tgl::loader_init();
    }
    return TRUE;
}
