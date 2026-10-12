#include "features/performance/worker_threads.h"

#include <windows.h>

#include <algorithm>

#include "hooks/iat.h"
#include "log.h"

namespace cp::features {

namespace {

// tbb::task_scheduler_init::initialize(int number_of_threads, stack_size_type thread_stack_size)
constexpr const char* kTbbInitialize = "?initialize@task_scheduler_init@tbb@@QAEXHI@Z";

// The target is __thiscall; __fastcall receives `this` in ecx the same way and leaves edx unused.
using InitializeFn = void(__fastcall*)(void* self, void* edx, int threads, unsigned stack_size);

InitializeFn g_original = nullptr;
int g_threads = 0;

void __fastcall initialize_hook(void* self, void* edx, int threads, unsigned stack_size) {
    logf("worker_threads: engine asked for %d thread(s), using %d", threads, g_threads);
    g_original(self, edx, g_threads, stack_size);
}

}  // namespace

void install_worker_threads(const Config& config) {
    if (config.engine_worker_threads <= 0) {
        logf("worker_threads: disabled");
        return;
    }
    g_threads = std::clamp(config.engine_worker_threads, 1, 64);

    void* original = hooks::hook_iat(GetModuleHandleW(nullptr), "tbb.dll", kTbbInitialize,
                                     reinterpret_cast<void*>(&initialize_hook));
    if (!original) {
        logf("worker_threads: tbb.dll!task_scheduler_init::initialize not found in the exe import table");
        return;
    }
    g_original = reinterpret_cast<InitializeFn>(original);
    logf("worker_threads: hooked, will use %d thread(s)", g_threads);
}

}  // namespace cp::features
