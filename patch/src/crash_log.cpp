#include "crash_log.h"

#include <windows.h>

#include <cstdint>

#include "log.h"

// Writes one block to the_grand_loader.log when the process is about to die from an unhandled fault, so a crash
// introduced by a patch can be attributed without a debugger. Continues the search afterwards: the
// game's own handling (and the Windows error dialog) is unchanged.

namespace cp {

namespace {

CrashContextFn g_context = nullptr;
LONG g_reported = 0;
DWORD g_main_thread = 0;  // the thread that installed the handler: the game's main thread (plugin init)

const char* code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        default: return "exception";
    }
}

bool is_fatal(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
           code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
           code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_IN_PAGE_ERROR;
}

LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (!is_fatal(code) || InterlockedCompareExchange(&g_reported, 1, 0) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const CONTEXT* c = info->ContextRecord;
    auto base = reinterpret_cast<uintptr_t>(host().exe_base);
    uintptr_t eip = c->Eip;
    // Which module the fault is in, and where: exe addresses also as listing VAs (base 0x00400000, as in
    // docs/re/decomp/), other modules as module+offset (performance_patch.dll offsets match its link map).
    HMODULE module = nullptr;
    char module_path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(eip), &module) && module) {
        GetModuleFileNameA(module, module_path, MAX_PATH);
    }
    const char* module_name = module_path;
    for (const char* p = module_path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            module_name = p + 1;
        }
    }
    if (eip >= base && eip < base + host().exe_size) {
        logf("crash: %s (0x%08lX) at %08X = v2game.exe listing %08X", code_name(code), code, eip,
             static_cast<unsigned>(eip - base + 0x00400000));
    } else {
        logf("crash: %s (0x%08lX) at %08X = %s+%X", code_name(code), code, eip, module_name,
             static_cast<unsigned>(eip - reinterpret_cast<uintptr_t>(module)));
    }
    logf("crash:   thread %lu (game thread %s)", GetCurrentThreadId(),
         GetCurrentThreadId() == g_main_thread ? "yes" : "no");
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        logf("crash:   %s address %08X", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
             static_cast<unsigned>(info->ExceptionRecord->ExceptionInformation[1]));
    }
    logf("crash:   eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X", c->Eax, c->Ebx, c->Ecx,
         c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // A few return addresses from the stack, unfiltered: anything inside the exe is a candidate caller.
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(c->Esp);
    int shown = 0;
    for (int i = 0; i < 512 && shown < 16; i++) {
        if (IsBadReadPtr(sp + i, 4)) {
            break;
        }
        uint32_t v = sp[i];
        if (v >= base && v < base + host().exe_size) {
            logf("crash:   stack[%3d] %08X", i, v);
            shown++;
        }
    }
    if (g_context) {
        g_context();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install_crash_log(CrashContextFn context) {
    g_context = context;
    g_main_thread = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, handler);
}

}  // namespace cp
