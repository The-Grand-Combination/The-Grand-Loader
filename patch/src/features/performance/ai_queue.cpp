#include "features/performance/ai_queue.h"
#include "features/performance/decision_cull.h"

#include <windows.h>

#include <cstdint>

#include "hooks/memory.h"
#include "log.h"

// v2game.exe 3.04 (docs/re/systems/tick.md, docs/re/systems/performance.md):
//
//   DayTick (0x006859C0), end: every AI object of world+0xA4 is appended to bucket (country % N) of the
//     job queue at world+0xB84 (16-byte list headers, +8 = job count); N = 1 in single player.
//   CInGameIdler_Update: if the first bucket is not empty, CALL AI_FrameUpdate (0x006874C0) at 0x006557C4
//     with a pointer to a local holding world+0xB84 (stdcall, RET 4). It pops jobs and runs
//     AI_CountryFrameJob until the bucket is empty or 0.02 s (double at 0x00E456C8) have passed.
//   ProcessGameTimeAdvance (0x00682BD0) issues the next hourly tick only when every bucket is empty.
//
// The thunk keeps every register, then calls AI_FrameUpdate again while the game runs at max speed
// (world+0xB28 == 4) and the bucket still holds jobs, up to kMaxFrameMs per frame.

namespace cp::features {

namespace {

constexpr uint32_t kCallSiteRva = 0x2557C4;      // CALL AI_FrameUpdate in CInGameIdler_Update
constexpr uint32_t kAiFrameUpdateRva = 0x2874C0;  // AI_FrameUpdate
constexpr uint32_t kWorldPtrRva = 0xE588E8;       // DAT_012588E8
constexpr uint32_t kWorldSpeed = 0xB28;           // 0..4
constexpr int32_t kMaxSpeed = 4;
constexpr int64_t kMaxFrameMs = 250;              // keeps the window responsive on a very heavy day

using AiFrameUpdateFn = void(__stdcall*)(void* queue_ref);
AiFrameUpdateFn g_ai_frame_update = nullptr;
uintptr_t g_world_slot = 0;
int64_t g_qpc_freq = 0;
int64_t g_max_frame_ticks = 0;  // kMaxFrameMs in QPC ticks

inline int64_t qpc() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

// `queue_ref` points to a pointer to the bucket array; bucket 0 +8 is its job count.
inline int32_t pending_jobs(void* queue_ref) {
    uintptr_t buckets = *static_cast<const uint32_t*>(queue_ref);
    return buckets ? *reinterpret_cast<const int32_t*>(buckets + 8) : 0;
}

void __stdcall run_ai_queue(void* queue_ref) {
    int64_t start = qpc();
    uintptr_t world = *reinterpret_cast<const uint32_t*>(g_world_slot);
    bool max_speed = world && *reinterpret_cast<const int32_t*>(world + kWorldSpeed) == kMaxSpeed;
    if (max_speed) {
        decisions_frame_begin(queue_ref);  // parallel_decisions; does nothing when that option is off
    }
    g_ai_frame_update(queue_ref);
    if (max_speed) {
        while (pending_jobs(queue_ref) > 0 && qpc() - start < g_max_frame_ticks) {
            g_ai_frame_update(queue_ref);
        }
        decisions_frame_end();
    }
}

// Stack at entry: [ret][queue_ref]. After pushad (32) + pushfd (4), queue_ref is at [esp+40]. The x87,
// MMX and SSE state is saved around the call with fxsave (the caller may keep XMM values across it,
// and parallel_decisions runs floating point code), in a 16-byte aligned area below the saved registers.
__declspec(naked) void ai_queue_thunk() {
    __asm {
        pushad
        pushfd
        mov ebx, esp
        sub esp, 512
        and esp, 0xFFFFFFF0
        fxsave [esp]
        push dword ptr [ebx + 40]
        call run_ai_queue
        fxrstor [esp]
        mov esp, ebx
        popfd
        popad
        ret 4
    }
}

}  // namespace

void install_ai_queue(const Config& config) {
    if (!config.fast_ai_queue) {
        logf("fast_ai_queue: disabled");
        return;
    }
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpc_freq = freq.QuadPart;
    g_max_frame_ticks = g_qpc_freq * kMaxFrameMs / 1000;
    g_world_slot = hooks::exe_addr(kWorldPtrRva);
    g_ai_frame_update = reinterpret_cast<AiFrameUpdateFn>(hooks::exe_addr(kAiFrameUpdateRva));
    if (!hooks::redirect_call(hooks::exe_addr(kCallSiteRva), hooks::exe_addr(kAiFrameUpdateRva),
                              reinterpret_cast<uintptr_t>(&ai_queue_thunk))) {
        logf("fast_ai_queue: call site %08X not recognised or already hooked, feature off", kCallSiteRva + 0x400000);
        return;
    }
    logf("fast_ai_queue: active, at max speed the AI job queue finishes in one frame (up to %lld ms)", static_cast<long long>(kMaxFrameMs));
}

}  // namespace cp::features
