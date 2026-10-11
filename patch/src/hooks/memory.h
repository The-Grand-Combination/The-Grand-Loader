#pragma once

#include <cstddef>
#include <cstdint>

namespace cp::hooks {

// Address of the exe image in this process.
uintptr_t exe_base();

// `rva` (VA - 0x00400000 in the 3.04 listing) resolved against the live image base.
inline uintptr_t exe_addr(uint32_t rva) { return exe_base() + rva; }

// True when `size` bytes at `at` equal `expected`.
bool expect_bytes(uintptr_t at, const void* expected, size_t size);

// Overwrites `size` bytes at `at` (any protection) and flushes the instruction cache.
bool write_bytes(uintptr_t at, const void* src, size_t size);

// Replaces the pointer stored at `slot` (a vtable entry or any code/data pointer) when it currently
// holds `expected`. Returns false and writes nothing otherwise.
bool replace_pointer(uintptr_t slot, uintptr_t expected, uintptr_t replacement);

// Redirects the 5-byte `CALL rel32` at `site` to `target` when it currently calls `expected_target`.
bool redirect_call(uintptr_t site, uintptr_t expected_target, uintptr_t target);

}  // namespace cp::hooks
