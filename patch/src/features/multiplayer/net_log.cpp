#include "features/multiplayer/net_log.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "game/world.h"
#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04: ENet is linked statically. enet_host_service (0x00A89D00, cdecl (host, event,
// timeout), returns > 0 with an event) is called from 16 places, the server's per-frame service and
// several client paths among them. The event is ENet's: type +0x00 (1 connect, 2 disconnect, 3 receive),
// peer +0x04, packet +0x10. The peer's address follows its host pointer: ENet 1.3 puts the host at
// +0x08 and the address at +0x18, ENet 1.2 the host at +0x00 and the address at +0x0C; which one this
// build uses is decided at the first event by checking where the host pointer is.
//
// The function's first six bytes (PUSH EBP; MOV EBP,ESP; AND ESP,-8) are replaced by a jump to a
// wrapper with the same signature, which calls the original through a trampoline (hooks::make_trampoline).

namespace cp::features {

namespace {

constexpr uint32_t kServiceRva = 0x689D00;
constexpr unsigned char kPrologue[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr uint32_t kEventConnect = 1;
constexpr uint32_t kEventDisconnect = 2;
constexpr uint32_t kEventReceive = 3;
constexpr DWORD kSilenceMs = 2000;
constexpr int kMaxHosts = 8;
constexpr int kMaxLines = 300;

using ServiceFn = int(__cdecl*)(void* host, uint32_t* event, uint32_t timeout);
ServiceFn g_original = nullptr;

struct HostState {
    uintptr_t host;
    int peers;            // connected peers seen through this host
    DWORD last_receive;   // GetTickCount of the last received packet
};
HostState g_hosts[kMaxHosts];
int g_address_offset = -1;  // into ENetPeer, decided at the first event
int g_lines = 0;

HostState& state_of(uintptr_t host) {
    for (HostState& s : g_hosts) {
        if (s.host == host) {
            return s;
        }
    }
    for (HostState& s : g_hosts) {
        if (s.host == 0) {
            s = {host, 0, 0};
            return s;
        }
    }
    return g_hosts[0];
}

void describe_peer(uintptr_t host, uintptr_t peer, char* out, size_t size) {
    if (g_address_offset < 0 && peer) {
        if (game::read32(peer + 8) == host) {
            g_address_offset = 0x18;
        } else if (game::read32(peer) == host) {
            g_address_offset = 0x0C;
        }
    }
    if (!peer || g_address_offset < 0) {
        std::snprintf(out, size, "peer %08X", static_cast<unsigned>(peer));
        return;
    }
    const auto* ip = reinterpret_cast<const unsigned char*>(peer + g_address_offset);
    uint16_t port = *reinterpret_cast<const uint16_t*>(peer + g_address_offset + 4);
    std::snprintf(out, size, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
}

void note_event(uintptr_t host, const uint32_t* event) {
    HostState& s = state_of(host);
    DWORD now = GetTickCount();
    uint32_t type = event[0];
    if (type == kEventReceive) {
        if (s.peers > 0 && s.last_receive && now - s.last_receive > kSilenceMs && g_lines++ < kMaxLines) {
            logf("network_log: host %08X: %lu ms without a packet, traffic again", static_cast<unsigned>(host),
                 now - s.last_receive);
        }
        s.last_receive = now;
        return;
    }
    if (type != kEventConnect && type != kEventDisconnect) {
        return;
    }
    char peer[48];
    describe_peer(host, event[1], peer, sizeof(peer));
    if (type == kEventConnect) {
        s.peers++;
        s.last_receive = now;
    } else if (s.peers > 0) {
        s.peers--;
    }
    if (g_lines++ < kMaxLines) {
        logf("network_log: host %08X: %s %s (%d connected)", static_cast<unsigned>(host),
             type == kEventConnect ? "CONNECT" : "DISCONNECT", peer, s.peers);
    }
}

int __cdecl service_hook(void* host, uint32_t* event, uint32_t timeout) {
    int result = g_original(host, event, timeout);
    if (result > 0 && event) {
        note_event(reinterpret_cast<uintptr_t>(host), event);
    }
    return result;
}

}  // namespace

void install_net_log(const Config& config) {
    if (!config.network_log) {
        logf("network_log: disabled");
        return;
    }
    uintptr_t service = hooks::exe_addr(kServiceRva);
    uintptr_t trampoline = 0;
    if (hooks::expect_bytes(service, kPrologue, sizeof(kPrologue))) {
        trampoline = hooks::make_trampoline(service, sizeof(kPrologue));
    }
    if (!trampoline) {
        logf("network_log: enet_host_service not recognised, off");
        return;
    }
    g_original = reinterpret_cast<ServiceFn>(trampoline);
    if (!hooks::jump_hook(service, kPrologue, sizeof(kPrologue), reinterpret_cast<uintptr_t>(&service_hook))) {
        logf("network_log: enet_host_service not patched, off");
        return;
    }
    logf("network_log: active, connections, disconnections and silences over %lu ms are logged", kSilenceMs);
}

}  // namespace cp::features
