#include "features/multiplayer/fpu_pin.h"

#include <cstdint>

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

constexpr uint16_t kPrecision24 = 0x0000;
constexpr uint16_t kPrecision53 = 0x0200;
constexpr uint16_t kPrecisionMask = 0x0300;
constexpr uint16_t kRoundingMask = 0x0C00;
constexpr uint16_t kExceptionMasks = 0x003F;
constexpr uint32_t kMxcsrDefault = 0x1F80;

uint16_t g_precision = kPrecision24;
bool g_logged = false;

void pin() {
    uint16_t cw = 0;
    uint32_t csr = 0;
    __asm {
        fnstcw cw
        stmxcsr csr
    }
    uint16_t wanted = static_cast<uint16_t>((cw & ~(kPrecisionMask | kRoundingMask)) | g_precision | kExceptionMasks);
    if (cw != wanted || csr != kMxcsrDefault) {
        if (!g_logged) {
            g_logged = true;
            logf("pin_fpu: first tick: x87 control word %04X -> %04X, MXCSR %08X -> %08X", cw, wanted, csr, kMxcsrDefault);
        }
        uint32_t fresh = kMxcsrDefault;
        __asm {
            fnclex
            fldcw wanted
            ldmxcsr fresh
        }
    } else if (!g_logged) {
        g_logged = true;
        logf("pin_fpu: first tick: x87 control word already %04X, MXCSR %08X, nothing to change", cw, csr);
    }
}

}  // namespace

void install_fpu_pin(const Config& config) {
    if (!config.pin_fpu) {
        logf("pin_fpu: disabled");
        return;
    }
    g_precision = config.fpu_precision == 53 ? kPrecision53 : kPrecision24;
    if (!hooks::on_hourly_tick(&pin)) {
        logf("pin_fpu: hourly tick hook unavailable, off");
        return;
    }
    logf("pin_fpu: active, %d-bit x87 precision, round to nearest, MXCSR default, set every tick",
         config.fpu_precision == 53 ? 53 : 24);
}

}  // namespace cp::features
