#include "parallel/worker_pool.h"

#include <windows.h>

#include <cstdint>

#include "hooks/fpu.h"
#include "log.h"

namespace cp::parallel {

namespace {

constexpr int kMaxWorkers = 7;

struct Pool {
    SRWLOCK lock = SRWLOCK_INIT;
    CONDITION_VARIABLE wake = CONDITION_VARIABLE_INIT;
    CONDITION_VARIABLE done = CONDITION_VARIABLE_INIT;
    uint32_t generation = 0;  // bumped once per batch
    int busy = 0;             // workers still inside the current batch
    TaskFn task = nullptr;
    void* context = nullptr;
    int count = 0;
    volatile LONG next = 0;   // next index to hand out
    uint16_t x87 = 0;
    uint32_t mxcsr = 0;
};
Pool g_pool;
int g_workers = -1;  // -1 = not started

void work_batch() {
    for (;;) {
        LONG i = InterlockedIncrement(&g_pool.next) - 1;
        if (i >= g_pool.count) {
            return;
        }
        g_pool.task(static_cast<int>(i), g_pool.context);
    }
}

DWORD WINAPI worker_main(void*) {
    uint32_t seen = 0;
    for (;;) {
        AcquireSRWLockExclusive(&g_pool.lock);
        while (g_pool.generation == seen) {
            SleepConditionVariableSRW(&g_pool.wake, &g_pool.lock, INFINITE, 0);
        }
        seen = g_pool.generation;
        ReleaseSRWLockExclusive(&g_pool.lock);

        hooks::write_fpu(g_pool.x87, g_pool.mxcsr);
        work_batch();

        AcquireSRWLockExclusive(&g_pool.lock);
        if (--g_pool.busy == 0) {
            WakeConditionVariable(&g_pool.done);
        }
        ReleaseSRWLockExclusive(&g_pool.lock);
    }
}

void start() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    int wanted = static_cast<int>(info.dwNumberOfProcessors) - 1;
    wanted = wanted < 0 ? 0 : (wanted > kMaxWorkers ? kMaxWorkers : wanted);  // 0 on one core: serial path
    g_workers = 0;
    for (int i = 0; i < wanted; i++) {
        HANDLE thread = CreateThread(nullptr, 0, &worker_main, nullptr, 0, nullptr);
        if (!thread) {
            break;
        }
        CloseHandle(thread);
        g_workers++;
    }
    logf("worker_pool: %d worker thread(s) started (%lu logical processors)", g_workers,
         static_cast<unsigned long>(info.dwNumberOfProcessors));
}

}  // namespace

int workers() { return g_workers < 0 ? 0 : g_workers; }

void run(int count, TaskFn task, void* context) {
    if (count <= 0) {
        return;
    }
    if (g_workers < 0) {
        start();
    }
    if (g_workers == 0 || count == 1) {
        for (int i = 0; i < count; i++) {
            task(i, context);
        }
        return;
    }
    uint16_t x87;
    uint32_t mxcsr;
    hooks::read_fpu(x87, mxcsr);
    AcquireSRWLockExclusive(&g_pool.lock);
    g_pool.task = task;
    g_pool.context = context;
    g_pool.count = count;
    g_pool.next = 0;
    g_pool.x87 = x87;
    g_pool.mxcsr = mxcsr;
    g_pool.busy = g_workers;
    g_pool.generation++;
    ReleaseSRWLockExclusive(&g_pool.lock);
    WakeAllConditionVariable(&g_pool.wake);

    work_batch();

    AcquireSRWLockExclusive(&g_pool.lock);
    while (g_pool.busy > 0) {
        SleepConditionVariableSRW(&g_pool.done, &g_pool.lock, INFINITE, 0);
    }
    ReleaseSRWLockExclusive(&g_pool.lock);
}

}  // namespace cp::parallel
