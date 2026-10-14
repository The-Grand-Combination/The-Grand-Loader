#include "version_label.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace tgl {

namespace {

// 0x00633826 in the 3.04 build: std::string(version, 8) inside the application constructor (0x00633800).
//   6A 08              push 8            ; length
//   BF 0F 00 00 00     mov  edi, 0xF     ; SSO capacity, untouched
//   68 4C 76 E0 00     push 0x00E0764C   ; "V2 v3.04"
constexpr uintptr_t kSiteRva = 0x233826;
constexpr unsigned char kExpected[8] = {0x6A, 0x08, 0xBF, 0x0F, 0x00, 0x00, 0x00, 0x68};
constexpr size_t kLengthOffset = 1;
constexpr size_t kPointerOffset = 8;

std::string g_label;

bool write_bytes(void* dst, const void* src, size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    std::memcpy(dst, src, size);
    VirtualProtect(dst, size, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), dst, size);
    return true;
}

}  // namespace

bool patch_version_label(void* exe_base, const char* suffix) {
    auto* site = static_cast<unsigned char*>(exe_base) + kSiteRva;
    if (std::memcmp(site, kExpected, sizeof(kExpected)) != 0) {
        return false;
    }

    const char* original = *reinterpret_cast<const char* const*>(site + kPointerOffset);
    g_label = std::string(original, site[kLengthOffset]) + suffix;
    if (g_label.size() > 127) {
        return false;  // `push imm8` cannot encode it
    }

    // One write covering both operands, so the site is never left with the new length and the old string.
    unsigned char patched[kPointerOffset + sizeof(const char*) - kLengthOffset];
    std::memcpy(patched, site + kLengthOffset, sizeof(patched));
    patched[0] = static_cast<unsigned char>(g_label.size());
    const char* pointer = g_label.c_str();
    std::memcpy(patched + (kPointerOffset - kLengthOffset), &pointer, sizeof(pointer));
    return write_bytes(site + kLengthOffset, patched, sizeof(patched));
}

}  // namespace tgl
