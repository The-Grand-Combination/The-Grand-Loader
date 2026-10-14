#pragma once

#include <cstdint>

namespace cp::hooks {

inline void read_fpu(uint16_t& x87, uint32_t& mxcsr) {
    uint16_t cw;
    uint32_t csr;
    __asm {
        fnstcw cw
        stmxcsr csr
    }
    x87 = cw;
    mxcsr = csr;
}

inline void write_fpu(uint16_t x87, uint32_t mxcsr) {
    __asm {
        fldcw x87
        ldmxcsr mxcsr
    }
}

}  // namespace cp::hooks
