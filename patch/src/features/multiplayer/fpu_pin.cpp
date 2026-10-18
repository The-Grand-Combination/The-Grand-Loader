#include "features/multiplayer/fpu_pin.h"

#include <cstdint>

#include "hooks/fpu.h"
#include "hooks/hourly_tick.h"
#include "log.h"

// The game creates its Direct3D 9 device without D3DCREATE_FPU_PRESERVE (docs/re/systems/multiplayer.md),
// so on Windows the game thread runs with 24-bit x87 precision afterwards, while a thread that never
// created a device (and a game under a D3D9 translation layer such as dxvk) keeps the C runtime's
// 53-bit setting. The statically linked CRT also chooses between its SSE2 and x87 implementations of
// pow/exp/log/floor on every call by looking at MXCSR (must be the default 0x1F80) and the x87
// exception masks (must all be set): a patch that enables flush-to-zero, or a stray unmasked
// exception, silently switches those functions to the other implementation, with different last bits.
//
// This feature writes both registers at the start of every hourly tick, before the day is simulated:
// precision from the ini (24 or 53 bits), rounding to nearest, all exceptions masked, MXCSR default.

namespace cp::features {

namespace {

uint16_t g_precision = hooks::kX87Precision24;
bool g_logged = false;

void pin() {
    uint16_t cw;
    uint32_t csr;
    hooks::read_fpu(cw, csr);
    constexpr uint16_t kReplaced = hooks::kX87PrecisionMask | hooks::kX87RoundingMask;
    auto wanted = static_cast<uint16_t>((cw & ~kReplaced) | g_precision | hooks::kX87ExceptionMasks);
    if (!g_logged) {
        g_logged = true;
        logf("pin_fpu: first tick: x87 control word %04X -> %04X, MXCSR %08X -> %08X", cw, wanted, csr,
             hooks::kMxcsrDefault);
    }
    if (cw != wanted || csr != hooks::kMxcsrDefault) {
        __asm fnclex
        hooks::write_fpu(wanted, hooks::kMxcsrDefault);
    }
}

}  // namespace

void install_fpu_pin(const Config& config) {
    if (!config.pin_fpu) {
        logf("pin_fpu: disabled");
        return;
    }
    g_precision = config.fpu_precision == 53 ? hooks::kX87Precision53 : hooks::kX87Precision24;
    if (!hooks::on_hourly_tick(&pin)) {
        logf("pin_fpu: hourly tick hook unavailable, off");
        return;
    }
    logf("pin_fpu: active, %d-bit x87 precision, round to nearest, MXCSR default, set every tick",
         g_precision == hooks::kX87Precision53 ? 53 : 24);
}

}  // namespace cp::features
