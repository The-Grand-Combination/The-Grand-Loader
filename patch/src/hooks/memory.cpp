#include "hooks/memory.h"

#include <windows.h>

#include <cstring>

namespace cp::hooks {

uintptr_t g_exe_base = 0;

void set_exe_base(const void* base) { g_exe_base = reinterpret_cast<uintptr_t>(base); }

bool expect_bytes(uintptr_t at, const void* expected, size_t size) {
    return std::memcmp(reinterpret_cast<const void*>(at), expected, size) == 0;
}

bool write_bytes(uintptr_t at, const void* src, size_t size) {
    void* dst = reinterpret_cast<void*>(at);
    DWORD old_protect = 0;
    if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }
    std::memcpy(dst, src, size);
    VirtualProtect(dst, size, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), dst, size);
    return true;
}

bool replace_pointer(uintptr_t slot, uintptr_t expected, uintptr_t replacement) {
    if (!expect_bytes(slot, &expected, sizeof(expected))) {
        return false;
    }
    return write_bytes(slot, &replacement, sizeof(replacement));
}

namespace {
bool redirect_branch(uint8_t opcode, uintptr_t site, uintptr_t expected_target, uintptr_t target) {
    unsigned char expected[5] = {opcode};
    int32_t rel = static_cast<int32_t>(expected_target - (site + 5));
    std::memcpy(expected + 1, &rel, sizeof(rel));
    if (!expect_bytes(site, expected, sizeof(expected))) {
        return false;
    }
    unsigned char patched[5] = {opcode};
    rel = static_cast<int32_t>(target - (site + 5));
    std::memcpy(patched + 1, &rel, sizeof(rel));
    return write_bytes(site, patched, sizeof(patched));
}

}  // namespace

bool redirect_call(uintptr_t site, uintptr_t expected_target, uintptr_t target) {
    return redirect_branch(0xE8, site, expected_target, target);
}

bool redirect_jump(uintptr_t site, uintptr_t expected_target, uintptr_t target) {
    return redirect_branch(0xE9, site, expected_target, target);
}

bool chain_call(uintptr_t site, uintptr_t target, uintptr_t& previous) {
    previous = 0;
    if (*reinterpret_cast<const unsigned char*>(site) != 0xE8) {
        return false;
    }
    int32_t rel;
    std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
    previous = site + 5 + static_cast<uintptr_t>(rel);
    return redirect_call(site, previous, target);
}

}  // namespace cp::hooks
