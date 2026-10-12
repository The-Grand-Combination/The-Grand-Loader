#include "features/performance/network_wait.h"

#include <windows.h>

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (re/systems/network.md):
//
//   CommandQueue_Process (0x009AF9F0) -> [obj+0x38]->vt[0x1C](0) -> ... -> FUN_00A61780 (network class, vtable
//   slot at 0x00E42744): enet_host_service(this+0x18 host, &event, this+0x20 timeout)    CALL at 0x00A618CC
//   enet_host_service (0x00A89D00, cdecl, 3 args) -> enet_socket_wait (0x00A8B400) -> select(..., timeout)
//
// The base constructor (0x00A5FF00) sets the timeout to 15 ms, the client and server variants (0x00A635C0,
// 0x00A63830) to 35 ms. The call site is redirected to a cdecl function that forwards with a timeout of 0.
// Only the time spent waiting for packets changes; every packet is still received and dispatched, a
// frame later at most.

namespace cp::features {

namespace {

constexpr uint32_t kServiceCallRva = 0x6618CC;  // CALL enet_host_service in FUN_00A61780
constexpr uint32_t kHostServiceRva = 0x689D00;  // enet_host_service

using HostServiceFn = int(__cdecl*)(void* host, void* event, uint32_t timeout);
HostServiceFn g_host_service = nullptr;
bool g_logged = false;

int __cdecl non_blocking_host_service(void* host, void* event, uint32_t timeout) {
    if (!g_logged) {
        g_logged = true;
        logf("skip_network_wait: first poll, the game asked for a %u ms wait, polling without one", timeout);
    }
    return g_host_service(host, event, 0);
}

}  // namespace

void install_network_wait(const Config& config) {
    if (!config.skip_network_wait) {
        logf("skip_network_wait: disabled");
        return;
    }
    g_host_service = reinterpret_cast<HostServiceFn>(hooks::exe_addr(kHostServiceRva));
    if (!hooks::redirect_call(hooks::exe_addr(kServiceCallRva), hooks::exe_addr(kHostServiceRva),
                              reinterpret_cast<uintptr_t>(&non_blocking_host_service))) {
        logf("skip_network_wait: call site %08X not recognised, feature off", kServiceCallRva + 0x400000);
        return;
    }
    logf("skip_network_wait: active, the per-frame network poll no longer blocks");
}

}  // namespace cp::features
