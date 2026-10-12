#include "features/performance/country_prepass.h"

#include <windows.h>

#include <cstdint>

#include "features/performance/pops_prefetch.h"
#include "features/performance/relations_prefetch.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/performance.md):
//
//   Country_PrePass (0x00508590) runs for all 683 tags every day, alive or not. Early on it calls
//   FUN_0051FD10(country) (stdcall 1, RET 4, CALL at 0x0050861B), which resets two per-country tables to
//   the constant pair at DAT_013F3688/DAT_013F368C:
//     country+0x1514  vector of `goods` 0x18-byte entries          (goods = count at DAT_012587F0 +0xC/+0x10)
//     country+0x14F4  vector of `types` 16-byte headers, each a row of `goods` 0x18-byte entries
//                     (types = count at DAT_0125CE80 +0xC/+0x10, a lazy singleton created by the call)
//   resizing both first when their sizes do not match. ~30 KB written per tag per day; the samples sit on
//   the stores (0x0051FEA4..). For a tag whose tables already hold the constant everywhere (a dead tag
//   nothing has written to since yesterday), the call changes nothing, so it is skipped: same memory, same
//   results. The check is a sequential read and stops at the first entry that differs.
//
//   Later in the same function, the loop at 0x00508DA7 reads relation +0x2C/+0x30 of the tag with every
//   tag (70 % of Country_PrePass's own samples at 0x00508DEE). The relation prefetch before
//   Country_Recalculate only runs for tags with provinces (its call at 0x005089DB is skipped otherwise),
//   so dead tags got none: they get it here. FUN_0050B9B0, also called from Country_PrePass, walks the
//   tag's pops (stall after [pop+0x27C] at 0x0050C089): they are prefetched here too.

namespace cp::features {

namespace {

constexpr uint32_t kResetCallRva = 0x10861B;   // CALL FUN_0051FD10 in Country_PrePass
constexpr uint32_t kResetRva = 0x11FD10;       // FUN_0051FD10
constexpr uint32_t kResetValueRva = 0xFF3688;  // DAT_013F3688 (+4: DAT_013F368C)
constexpr uint32_t kGoodsSourceRva = 0xE587F0; // DAT_012587F0: +0xC/+0x10 vector, count = goods
constexpr uint32_t kTypesSourceRva = 0xE5CE80; // DAT_0125CE80: lazy singleton, +0xC/+0x10 vector
constexpr uint32_t kCountryRow = 0x1514;
constexpr uint32_t kCountryMatrix = 0x14F4;
constexpr uint32_t kCountryProvincesBegin = 0x9D8;
constexpr uint32_t kCountryProvincesEnd = 0x9DC;
constexpr uint32_t kEntrySize = 0x18;
constexpr uint64_t kReportEvery = 683 * 30;    // about every 30 game days

bool g_skip_resets = false;
uintptr_t g_reset = 0;
uintptr_t g_reset_value = 0;
uintptr_t g_goods_source = 0;
uintptr_t g_types_source = 0;
uint8_t g_skip = 0;
uint64_t g_calls = 0;
uint64_t g_skipped = 0;

inline uint32_t read32(uintptr_t at) { return *reinterpret_cast<const uint32_t*>(at); }

inline int32_t vector_count(uintptr_t vec, uint32_t element) {
    return static_cast<int32_t>((read32(vec + 4) - read32(vec)) / element);
}

// True when every entry of `count` 0x18-byte entries at `row` already holds the reset pair.
bool row_clean(uintptr_t row, int32_t count, uint32_t lo, uint32_t hi) {
    const uint32_t* p = reinterpret_cast<const uint32_t*>(row);
    for (int32_t i = 0; i < count; i++, p += 6) {
        if (p[0] != lo || p[1] != hi || p[2] != lo || p[3] != hi || p[4] != lo || p[5] != hi) {
            return false;
        }
    }
    return true;
}

// Whether FUN_0051FD10(country) would leave memory exactly as it is.
bool tables_clean(uintptr_t country) {
    uintptr_t goods_source = read32(g_goods_source);
    uintptr_t types_source = read32(g_types_source);
    if (!goods_source || !types_source) {
        return false;  // the call would create the lazy singleton
    }
    int32_t goods = static_cast<int32_t>((read32(goods_source + 0x10) - read32(goods_source + 0xC)) >> 2);
    int32_t types = static_cast<int32_t>((read32(types_source + 0x10) - read32(types_source + 0xC)) >> 2);
    if (vector_count(country + kCountryRow, kEntrySize) != goods || vector_count(country + kCountryMatrix, 16) != types) {
        return false;  // the call would resize
    }
    uint32_t lo = read32(g_reset_value), hi = read32(g_reset_value + 4);
    if (!row_clean(read32(country + kCountryRow), goods, lo, hi)) {
        return false;
    }
    uintptr_t headers = read32(country + kCountryMatrix);
    for (int32_t t = 0; t < types; t++) {
        if (!row_clean(read32(headers + static_cast<uint32_t>(t) * 16), goods, lo, hi)) {
            return false;
        }
    }
    return true;
}

// Returns 1 when the reset can be skipped.
uint8_t __stdcall before_reset(uintptr_t country) {
    if (!country) {
        return 0;
    }
    if (read32(country + kCountryProvincesEnd) == read32(country + kCountryProvincesBegin)) {
        prefetch_all_relations(country, 0x2C);  // the dead tag's relation loop (0x00508DA7)
        prefetch_all_relations(country, 0x30);
    } else {
        prefetch_country_pops(country);  // FUN_0050B9B0's pop walk
    }
    if (!g_skip_resets) {
        return 0;
    }
    uint8_t skip = tables_clean(country) ? 1 : 0;
    g_skipped += skip;
    if (++g_calls % kReportEvery == 0) {
        logf("skip_clean_resets: %llu of %llu per-country table resets skipped (already clean)",
             static_cast<unsigned long long>(g_skipped), static_cast<unsigned long long>(g_calls));
    }
    return skip;
}

// Stack at entry: [ret][country]. After pushad (32) + pushfd (4), country is at [esp+40].
__declspec(naked) void reset_thunk() {
    __asm {
        pushad
        pushfd
        push dword ptr [esp + 40]
        call before_reset
        mov byte ptr [g_skip], al
        popfd
        popad
        cmp byte ptr [g_skip], 0
        jne skip
        jmp dword ptr [g_reset]
    skip:
        ret 4                           ; stdcall, as the original
    }
}

}  // namespace

void install_country_prepass(const Config& config) {
    g_skip_resets = config.skip_clean_resets;
    if (!g_skip_resets && !config.prefetch_relations && !config.prefetch_pops) {
        logf("country_prepass: disabled (skip_clean_resets, prefetch_relations and prefetch_pops are off)");
        return;
    }
    g_reset = hooks::exe_addr(kResetRva);
    g_reset_value = hooks::exe_addr(kResetValueRva);
    g_goods_source = hooks::exe_addr(kGoodsSourceRva);
    g_types_source = hooks::exe_addr(kTypesSourceRva);
    if (!hooks::expect_bytes(g_reset, "\x55\x8B\xEC\x6A\xFF", 5) ||
        !hooks::redirect_call(hooks::exe_addr(kResetCallRva), g_reset, reinterpret_cast<uintptr_t>(&reset_thunk))) {
        logf("country_prepass: call site %08X not recognised or already hooked, off", kResetCallRva + 0x400000);
        return;
    }
    logf("country_prepass: active (skip_clean_resets %s; dead-tag relation prefetch and pop prefetch follow their own options)",
         g_skip_resets ? "on" : "off");
}

}  // namespace cp::features
