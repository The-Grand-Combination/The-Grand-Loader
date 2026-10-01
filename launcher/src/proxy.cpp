#include <windows.h>

#include <unknwn.h>

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInput8CreateFn resolve_system_dinput8() {
    static DirectInput8CreateFn cached = nullptr;
    if (cached) {
        return cached;
    }

    wchar_t path[MAX_PATH];
    UINT len = GetSystemDirectoryW(path, MAX_PATH);
    if (len == 0 || len + 13 > MAX_PATH) {
        return nullptr;
    }
    wcscat_s(path, L"\\dinput8.dll");

    HMODULE system = LoadLibraryW(path);
    if (!system) {
        return nullptr;
    }
    cached = reinterpret_cast<DirectInput8CreateFn>(GetProcAddress(system, "DirectInput8Create"));
    return cached;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out,
                                             LPUNKNOWN outer) {
    DirectInput8CreateFn real = resolve_system_dinput8();
    if (!real) {
        return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    }
    return real(inst, version, riid, out, outer);
}
