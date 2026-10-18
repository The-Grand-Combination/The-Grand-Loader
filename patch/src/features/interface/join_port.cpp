#include "features/interface/join_port.h"

#include <cstdint>
#include <cstring>

#include "game/string.h"
#include "game/world.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// v2game.exe 3.04:
//
//   JoinIpDialog_OnJoin (0x00767140) builds the connection request with
//     0x0076729B  CALL ConnectRequest_ctor (0x009F8180, cdecl, the caller cleans)
//   arguments [esp+4] the request, [esp+8] "LobbySession", [esp+0xC] the typed address (std::string*),
//   [esp+0x10] the port, always 0x666 = 1638, [esp+0x14] the typed address again, then more. The
//   request copies the strings (host at +0x40, the other at +0x24) and the port (+0x68); NetClient_ctor
//   (0x00A635C0) resolves the host with gethostbyname, then inet_addr, so "host:port" resolves to
//   nothing and the client connects nowhere. The server listens on 1638 as well (UDP).
//
// The CALL goes to a thunk that, for "host:port" with a port of 1-65535, points both address
// arguments at a copy holding only the host, sets the port, and jumps to the original.
//
// Once connected, the client lobby (FUN_0076BA60, when [0x012589AC] == 2) copies the session's host
// string (session+0xF4: the host alone) into the "lasthost" setting and saves settings.txt:
//     0x0076BE75  LEA ECX,[EAX+0x124]
//     0x0076BE7B  CALL std::string::assign (0x00408ED0, thiscall (src, pos, count), RET 0xC)
// That CALL goes to a second thunk which, when the source is the host this feature split off, assigns
// the full "host:port" instead, so the join dialog starts with the port next time.

namespace cp::features {

namespace {

constexpr uint32_t kCallRva = 0x36729B;
constexpr uint32_t kPortPushRva = 0x36727B;  // PUSH 0x666 before it
constexpr unsigned char kPortPushBytes[] = {0x68, 0x66, 0x06, 0x00, 0x00};
constexpr uint32_t kLobbyAssignRva = 0x36BE7B;
constexpr unsigned char kLobbyLeaBytes[] = {0x8D, 0x88, 0x24, 0x01, 0x00, 0x00};  // LEA ECX,[EAX+0x124]
constexpr uint32_t kRequestRva = 0x5F8180;
constexpr uint32_t kDefaultPort = 0x666;
constexpr size_t kMaxHost = 255;
constexpr size_t kMaxText = 263;  // host, colon, five digits, room to spare

uintptr_t g_request = 0;
uintptr_t g_assign = 0;
char g_host_text[kMaxHost + 1];
game::GameString g_host;  // read (copied) by the request before the dialog code runs again
char g_full_text[kMaxText + 1];
game::GameString g_full;  // the typed "host:port", for the lobby's copy into "lasthost"

bool g_logged = false;

// Rewrites the address and port arguments in place when the address carries a port.
void __cdecl split_address(uint32_t* args) {
    if (args[1] != kDefaultPort) {
        return;
    }
    const auto* typed = reinterpret_cast<const game::GameString*>(args[0]);
    const char* text = typed->c_str();
    const char* colon = std::strrchr(text, ':');
    if (!colon || colon == text || std::strchr(text, ':') != colon) {
        return;  // no port, an empty host, or more than one colon (IPv6 is not supported by the game)
    }
    uint32_t port = 0;
    const char* digit = colon + 1;
    for (; *digit >= '0' && *digit <= '9' && port <= 65535; digit++) {
        port = port * 10 + static_cast<uint32_t>(*digit - '0');
    }
    size_t length = static_cast<size_t>(colon - text);
    if (*digit != '\0' || digit == colon + 1 || port == 0 || port > 65535 || length > kMaxHost) {
        return;
    }
    size_t full = static_cast<size_t>(digit - text);
    if (full > kMaxText) {
        return;
    }
    std::memcpy(g_host_text, text, length);
    g_host_text[length] = '\0';
    g_host = game::view_of(g_host_text, length);
    std::memcpy(g_full_text, text, full);
    g_full_text[full] = '\0';
    g_full = game::view_of(g_full_text, full);
    uint32_t host = reinterpret_cast<uint32_t>(&g_host);
    if (args[2] == args[0]) {
        args[2] = host;
    }
    args[0] = host;
    args[1] = port;
    if (!g_logged) {
        g_logged = true;
        logf("join_port: joining %s on port %u", g_host_text, port);
    }
}

// [esp] return, [esp+4] request, [esp+8] name, [esp+0xC] address, [esp+0x10] port, [esp+0x14] address.
__declspec(naked) void request_thunk() {
    CP_SAVE_ALL
    __asm {
        lea eax, [esp + CP_SAVED_ALL_BYTES + 12]  // the address argument
        push eax
        call split_address
        add esp, 4
    }
    CP_RESTORE_ALL
    __asm jmp dword ptr [g_request]
}

// The lobby's assignment of the session host to "lasthost": the full "host:port" when the host is the
// one split off above. args points at the source argument.
void __cdecl keep_port(uint32_t* args) {
    if (g_full.size == 0) {
        return;
    }
    const auto* source = reinterpret_cast<const game::GameString*>(args[0]);
    if (source->size == g_host.size && std::strcmp(source->c_str(), g_host_text) == 0) {
        args[0] = reinterpret_cast<uint32_t>(&g_full);
    }
}

// thiscall assign: ECX = settings+0x124, [esp+4] source, [esp+8] position, [esp+0xC] count.
__declspec(naked) void lobby_assign_thunk() {
    CP_SAVE_ALL
    __asm {
        lea eax, [esp + CP_SAVED_ALL_BYTES + 4]  // the source argument
        push eax
        call keep_port
        add esp, 4
    }
    CP_RESTORE_ALL
    __asm jmp dword ptr [g_assign]
}

}  // namespace

void install_join_port(const Config& config) {
    if (!config.join_port) {
        logf("join_port: disabled");
        return;
    }
    g_request = hooks::exe_addr(kRequestRva);
    if (!hooks::expect_bytes(hooks::exe_addr(kPortPushRva), kPortPushBytes, sizeof(kPortPushBytes)) ||
        !hooks::redirect_call(hooks::exe_addr(kCallRva), g_request, reinterpret_cast<uintptr_t>(&request_thunk))) {
        logf("join_port: join dialog site %08X not recognised, off", hooks::listing_va(kCallRva));
        return;
    }
    g_assign = hooks::exe_addr(game::kStringAssignRva);
    uintptr_t lobby_site = hooks::exe_addr(kLobbyAssignRva);
    if (!hooks::expect_bytes(lobby_site - sizeof(kLobbyLeaBytes), kLobbyLeaBytes, sizeof(kLobbyLeaBytes)) ||
        !hooks::redirect_call(lobby_site, g_assign, reinterpret_cast<uintptr_t>(&lobby_assign_thunk))) {
        logf("join_port: lobby site %08X not recognised, the saved address will lack the port",
             hooks::listing_va(kLobbyAssignRva));
    }
    logf("join_port: active, the join dialog accepts address:port (the game's port is 1638)");
}

}  // namespace cp::features
