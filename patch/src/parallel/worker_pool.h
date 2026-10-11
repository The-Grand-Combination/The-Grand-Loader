#pragma once

namespace cp::parallel {

using TaskFn = void (*)(int index, void* context);

// Runs task(i, context) for every i in [0, count) on the worker threads and the calling thread, and
// returns when all are done. The workers start on the first call (not under the loader lock) and run
// each batch with the caller's x87 control word and MXCSR, so floating point behaves as on the caller.
// One batch at a time; call it from the game thread only.
void run(int count, TaskFn task, void* context);

// Worker threads besides the caller (0 until the first run).
int workers();

}  // namespace cp::parallel
