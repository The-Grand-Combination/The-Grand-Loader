#include "features/interface/chat_lines.h"

#include <cstdint>
#include <deque>

#include "game/world.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"
#include "util/clock.h"

// v2game.exe 3.04: Chat_ExpireLines (0x006784B0, EDI = chat window), called by CInGameIdler_Update at
// 0x00655477 every frame, drops the oldest line of the chat list (this+0x1638) when there are more lines
// than fit, or when
//     0x006784FD  CMP dword ptr [EBP-4],0x3C      more than 60 seconds have passed
// since the last removal (interface clock at this+0x1978, time of the last removal, or of the last frame
// with an empty list, as a double at this+0xE0). Lines carry no time of their own, so lines that arrive
// together leave one per period: the third of three lines stays three periods. The multiplayer lag
// warning ("X is lagging behind, decreasing speed.") repeats while a player lags and piles up this way.
//
// Each line gets its own time here: before the call, the line count (list vt[0x28], thiscall, no
// arguments) is compared with the times recorded so far (new lines are stamped now, lines that went are
// dropped from the front), and when the oldest line is older than chat_line_seconds, this+0xE0 is moved
// far into the past so the game's own code removes that line on this call. The immediate becomes 127 so
// the game's period never removes a line earlier.

namespace cp::features {

namespace {

constexpr uint32_t kCallSiteRva = 0x255477;  // CALL Chat_ExpireLines in CInGameIdler_Update
constexpr uint32_t kExpireRva = 0x2784B0;
constexpr uint32_t kCompareRva = 0x2784FD;
constexpr unsigned char kCompareBytes[] = {0x83, 0x7D, 0xFC, 0x3C};  // CMP dword ptr [EBP-4],0x3C
constexpr unsigned char kGameSeconds = 127;  // the immediate is a signed byte
constexpr uint32_t kChatList = 0x1638;
constexpr uint32_t kListCountSlot = 0x28;
constexpr uint32_t kLastRemoval = 0xE0;
constexpr double kFarPast = 1.0e6;  // seconds
constexpr int kVanillaSeconds = 60;
constexpr int kMaxSeconds = 3600;

uintptr_t g_expire = 0;
int64_t g_lifetime_ms = 0;
std::deque<int64_t> g_stamps;  // per line of the list, oldest first: when it was first seen (ms)

void __cdecl before_expire(uintptr_t chat) {
    uintptr_t list = game::read32(chat + kChatList);
    if (!list) {
        return;
    }
    auto count_fn = reinterpret_cast<int(__thiscall*)(uintptr_t)>(game::read32(game::read32(list) + kListCountSlot));
    int lines = count_fn(list);
    size_t count = lines > 0 ? static_cast<size_t>(lines) : 0;
    int64_t now = static_cast<int64_t>(util::qpc_ms(util::qpc()));
    while (g_stamps.size() > count) {
        g_stamps.pop_front();  // removed by the game (too many lines) or by the previous call
    }
    while (g_stamps.size() < count) {
        g_stamps.push_back(now);
    }
    if (!g_stamps.empty() && now - g_stamps.front() >= g_lifetime_ms) {
        *reinterpret_cast<double*>(chat + kLastRemoval) -= kFarPast;
        g_stamps.pop_front();
    }
}

// In place of the CALL: EDI = chat window, no stack arguments.
__declspec(naked) void expire_thunk() {
    __asm {
        CP_SAVE_ALL
        push edi
        call before_expire
        add esp, 4
        CP_RESTORE_ALL
        jmp dword ptr [g_expire]
    }
}

}  // namespace

void install_chat_lines(const Config& config) {
    int seconds = config.chat_line_seconds;
    if (seconds <= 0 || seconds == kVanillaSeconds) {
        logf("chat_line_seconds: vanilla (one chat line leaves every %d seconds)", kVanillaSeconds);
        return;
    }
    if (seconds > kMaxSeconds) {
        seconds = kMaxSeconds;
    }
    g_lifetime_ms = static_cast<int64_t>(seconds) * 1000;
    g_expire = hooks::exe_addr(kExpireRva);
    uintptr_t compare = hooks::exe_addr(kCompareRva);
    if (!hooks::expect_bytes(compare, kCompareBytes, sizeof(kCompareBytes)) ||
        !hooks::redirect_call(hooks::exe_addr(kCallSiteRva), g_expire, reinterpret_cast<uintptr_t>(&expire_thunk)) ||
        !hooks::write_bytes(compare + 3, &kGameSeconds, 1)) {
        logf("chat_line_seconds: site %08X or %08X not recognised, off", hooks::listing_va(kCallSiteRva),
             hooks::listing_va(kCompareRva));
        return;
    }
    logf("chat_line_seconds: active, every chat line goes %d seconds after it appeared (vanilla: one line "
         "every %d seconds)",
         seconds, kVanillaSeconds);
}

}  // namespace cp::features
