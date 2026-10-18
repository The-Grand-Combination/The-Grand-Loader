#include "features/performance/background_sleep.h"

#include <cstdint>
#include <iterator>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04. Both frame loops sleep before pumping messages:
//
//   App_MainLoop (0x009DF550)                      0x009DF677  CMP BYTE PTR [EAX+0x6BCA0],0
//                                                  0x009DF67E  JZ +4; PUSH 0; JMP +2
//                                                  0x009DF684  PUSH 100
//                                                  0x009DF686  CALL [Sleep]
//   App_ProcessMessagePumpAndUpdate (0x009DF2B0)   the same shape at 0x009DF2CD..0x009DF2DC
//
// renderDevice+0x6BCA0 is cleared when Present returns D3DERR_DEVICELOST (FUN_005F8470, FUN_006BA220,
// FUN_0099A520) and set again when the device is created or TestCooperativeLevel succeeds (FUN_0099BF60),
// i.e. it is clear while a fullscreen game has lost the screen. Only the PUSH immediate changes.

namespace cp::features {

namespace {

struct Site {
    uint32_t rva;  // of the PUSH imm8
    const char* loop;
};

constexpr Site kSites[] = {{0x5DF684, "main loop"}, {0x5DF2D5, "frame pump"}};
constexpr unsigned char kSiteBytes[] = {0x6A, 0x64, 0xFF, 0x15};  // PUSH 100; CALL [..]

}  // namespace

void install_background_sleep(const Config& config) {
    int ms = config.background_sleep_ms;
    if (ms < 0 || ms >= 100) {
        logf("background_sleep_ms: vanilla (100 ms per frame while the render device is lost)");
        return;
    }
    int sites = 0;
    for (const Site& site : kSites) {
        uintptr_t at = hooks::exe_addr(site.rva);
        unsigned char imm = static_cast<unsigned char>(ms);
        if (hooks::expect_bytes(at, kSiteBytes, sizeof(kSiteBytes)) && hooks::write_bytes(at + 1, &imm, 1)) {
            sites++;
        } else {
            logf("background_sleep_ms: %s site %08X not recognised, left alone", site.loop,
                 hooks::listing_va(site.rva));
        }
    }
    logf("background_sleep_ms: active on %d of %d sites, %d ms per frame while the render device is lost", sites,
         static_cast<int>(std::size(kSites)), ms);
}

}  // namespace cp::features
