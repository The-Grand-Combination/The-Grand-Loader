#include "hooks/memory.h"

#include <windows.h>

#include <cstring>

namespace se::hooks {

namespace {

constexpr unsigned char kCall = 0xE8;
constexpr unsigned char kJmp = 0xE9;
constexpr unsigned char kNop = 0x90;
constexpr size_t kRel32Size = 5;
constexpr size_t kMaxReplaced = 32;

void encode_rel32(unsigned char opcode, uintptr_t site, uintptr_t target, unsigned char* out) {
    out[0] = opcode;
    auto rel = static_cast<int32_t>(target - (site + kRel32Size));
    std::memcpy(out + 1, &rel, sizeof(rel));
}

void encode_call(uintptr_t site, uintptr_t target, unsigned char* out) { encode_rel32(kCall, site, target, out); }

}  // namespace

namespace detail {
uintptr_t g_exe_base = 0;
}

void set_exe_base(const void* base) { detail::g_exe_base = reinterpret_cast<uintptr_t>(base); }

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

bool redirect_call(uintptr_t site, uintptr_t expected_target, uintptr_t target) {
    unsigned char expected[kRel32Size];
    encode_call(site, expected_target, expected);
    if (!expect_bytes(site, expected, sizeof(expected))) {
        return false;
    }
    unsigned char patched[kRel32Size];
    encode_call(site, target, patched);
    return write_bytes(site, patched, sizeof(patched));
}

uintptr_t make_trampoline(uintptr_t site, size_t size) {
    if (size < kRel32Size || size > kMaxReplaced) {
        return 0;
    }
    auto* code = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, size + kRel32Size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) {
        return 0;
    }
    auto at = reinterpret_cast<uintptr_t>(code);
    std::memcpy(code, reinterpret_cast<const void*>(site), size);
    encode_rel32(kJmp, at + size, site + size, code + size);
    FlushInstructionCache(GetCurrentProcess(), code, size + kRel32Size);
    return at;
}

bool jump_hook(uintptr_t site, const void* expected, size_t size, uintptr_t target) {
    if (size < kRel32Size || size > kMaxReplaced || !expect_bytes(site, expected, size)) {
        return false;
    }
    unsigned char patched[kMaxReplaced];
    std::memset(patched, kNop, size);
    encode_rel32(kJmp, site, target, patched);
    return write_bytes(site, patched, size);
}

}  // namespace se::hooks
