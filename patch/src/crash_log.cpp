#include "crash_log.h"

#include <windows.h>

#include <cstdint>

#include "log.h"

// Writes one block to v2cp.log when the process is about to die from an unhandled fault, so a crash
// introduced by a patch can be attributed without a debugger. Continues the search afterwards: the
// game's own handling (and the Windows error dialog) is unchanged.

namespace cp {

namespace {

CrashContextFn g_context = nullptr;
LONG g_reported = 0;

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
    logf("crash: %s (0x%08lX) at %08X%s", code_name(code), code, eip,
         (eip >= base && eip < base + host().exe_size) ? " (in v2game.exe)" : "");
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
    AddVectoredExceptionHandler(1, handler);
}

}  // namespace cp
