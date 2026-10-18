#include "features/interface/chat_lines.h"

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04: Chat_ExpireLines (0x006784B0), called by CInGameIdler_Update every frame, measures the
// seconds since the last removal (interface clock at this+0x1978, last removal at this+0xE0) and drops
// the oldest line of the chat list (this+0x1638) when there are more lines than fit, or when
//     0x006784FD  CMP dword ptr [EBP-4],0x3C      more than 60 seconds have passed
// So every line lives about a minute, and lines queued behind it leave one per minute. The multiplayer
// lag warning ("X is lagging behind, decreasing speed.") is a chat line sent to every player by the
// host (CClientPingCommand execute 0x0072F1E0), and it repeats while a player lags, so it stays for
// minutes. Only the immediate changes.

namespace cp::features {

namespace {

constexpr uint32_t kCompareRva = 0x2784FD;
constexpr unsigned char kCompareBytes[] = {0x83, 0x7D, 0xFC, 0x3C};  // CMP dword ptr [EBP-4],0x3C
constexpr int kVanillaSeconds = 60;
constexpr int kMaxSeconds = 127;  // the immediate is a signed byte

}  // namespace

void install_chat_lines(const Config& config) {
    int seconds = config.chat_line_seconds;
    if (seconds <= 0 || seconds == kVanillaSeconds) {
        logf("chat_line_seconds: vanilla (a chat line every %d seconds)", kVanillaSeconds);
        return;
    }
    if (seconds > kMaxSeconds) {
        seconds = kMaxSeconds;
    }
    uintptr_t at = hooks::exe_addr(kCompareRva);
    unsigned char value = static_cast<unsigned char>(seconds);
    if (!hooks::expect_bytes(at, kCompareBytes, sizeof(kCompareBytes)) || !hooks::write_bytes(at + 3, &value, 1)) {
        logf("chat_line_seconds: site %08X not recognised, off", hooks::listing_va(kCompareRva));
        return;
    }
    logf("chat_line_seconds: active, the oldest chat line goes after %d seconds (vanilla %d)", seconds,
         kVanillaSeconds);
}

}  // namespace cp::features
