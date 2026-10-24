#pragma once

#include <cstddef>
#include <cstdint>

namespace se::hooks {

// Set once from TGL_PluginInit, before any feature is installed.
void set_exe_base(const void* base);

namespace detail {
extern uintptr_t g_exe_base;  // written once by set_exe_base
}
inline uintptr_t exe_base() { return detail::g_exe_base; }

// The 3.04 listing (docs/re/) puts the exe at 0x00400000; at runtime it is relocated.
constexpr uint32_t kListingBase = 0x00400000;

// A listing VA (docs/re/symbols) resolved against the live image base.
inline uintptr_t exe_va(uint32_t va) { return exe_base() + (va - kListingBase); }

bool expect_bytes(uintptr_t at, const void* expected, size_t size);

// Overwrites `size` bytes at `at` (any protection) and flushes the instruction cache.
bool write_bytes(uintptr_t at, const void* src, size_t size);

// Replaces the pointer stored at `slot` (a vtable entry, an imm32 operand) when it currently holds
// `expected`. Returns false and writes nothing otherwise.
bool replace_pointer(uintptr_t slot, uintptr_t expected, uintptr_t replacement);

// Redirects the 5-byte `CALL rel32` at `site` to `target` when it currently calls `expected_target`.
bool redirect_call(uintptr_t site, uintptr_t expected_target, uintptr_t target);

// Executable copy of the `size` bytes at `site` (whole instructions without relative or relocated
// operands) followed by a jump to `site + size`. 0 on failure.
uintptr_t make_trampoline(uintptr_t site, size_t size);

// Replaces the `size` bytes at `site` (at least 5, whole instructions, currently `expected`) with a
// `JMP rel32` to `target` padded with NOPs.
bool jump_hook(uintptr_t site, const void* expected, size_t size, uintptr_t target);

}  // namespace se::hooks
