#include <windows.h>

#include <unknwn.h>

#include <iterator>

namespace {

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInput8CreateFn resolve_system_dinput8() {
    static DirectInput8CreateFn cached = nullptr;
    if (cached) {
        return cached;
    }

    constexpr wchar_t kDll[] = L"\\dinput8.dll";
    wchar_t path[MAX_PATH];
    UINT len = GetSystemDirectoryW(path, MAX_PATH);
    if (len == 0 || len + std::size(kDll) > MAX_PATH) {
        return nullptr;
    }
    wcscat_s(path, kDll);

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
