#include "crash_log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "log.h"

namespace cp {

namespace {

constexpr DWORD kCxxException = 0xE06D7363;  // MSVC C++ throw
constexpr DWORD kFatalAppExit = 0x40000015;  // abort()
constexpr int kMaxThrowLogs = 12;

CrashContextFn g_context = nullptr;
LONG g_reported = 0;
LONG g_throws_logged = 0;
DWORD g_main_thread = 0;  // the thread that installed the handler: the game's main thread (plugin init)

const char* code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case kFatalAppExit: return "abort() (STATUS_FATAL_APP_EXIT: terminate() after an uncaught C++ exception, or a CRT assert)";
        default: return "exception";
    }
}

bool is_fatal(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
           code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
           code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_IN_PAGE_ERROR || code == kFatalAppExit;
}

// MSVC x86 RTTI for a thrown object: ExceptionInformation[2] is the _ThrowInfo.
struct TypeDescriptor { const void* vftable; void* spare; char name[1]; };
struct CatchableType { uint32_t properties; TypeDescriptor* type; };
struct CatchableTypeArray { int count; CatchableType* types[1]; };
struct ThrowInfo { uint32_t attributes; void* unwind; void* forward_compat; CatchableTypeArray* catchable; };

const char* thrown_type_name(const EXCEPTION_RECORD* r) {
    if (r->NumberParameters < 3) {
        return "?";
    }
    const ThrowInfo* ti = reinterpret_cast<const ThrowInfo*>(r->ExceptionInformation[2]);
    if (IsBadReadPtr(ti, sizeof(*ti)) || IsBadReadPtr(ti->catchable, sizeof(CatchableTypeArray)) ||
        ti->catchable->count < 1 || IsBadReadPtr(ti->catchable->types[0], sizeof(CatchableType)) ||
        IsBadReadPtr(ti->catchable->types[0]->type, 12)) {
        return "?";
    }
    return ti->catchable->types[0]->type->name;  // e.g. ".?AVbad_alloc@std@@"
}

// The catchable-type array lists every base the object can be caught as.
bool derives_from_std_exception(const EXCEPTION_RECORD* r) {
    const ThrowInfo* ti = reinterpret_cast<const ThrowInfo*>(r->ExceptionInformation[2]);
    if (IsBadReadPtr(ti, sizeof(*ti)) || IsBadReadPtr(ti->catchable, sizeof(CatchableTypeArray))) {
        return false;
    }
    int count = ti->catchable->count;
    if (count < 1 || count > 64 ||
        IsBadReadPtr(ti->catchable->types, static_cast<UINT_PTR>(count) * sizeof(CatchableType*))) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const CatchableType* ct = ti->catchable->types[i];
        if (!IsBadReadPtr(ct, sizeof(*ct)) && !IsBadReadPtr(ct->type, 32) &&
            std::strcmp(ct->type->name, ".?AVexception@std@@") == 0) {
            return true;
        }
    }
    return false;
}

// std::exception::what() is vtable slot 1 on MSVC; called only for objects catchable as std::exception.
const char* thrown_what(const EXCEPTION_RECORD* r) {
    if (r->NumberParameters < 3 || !derives_from_std_exception(r)) {
        return "";
    }
    const void* object = reinterpret_cast<const void*>(r->ExceptionInformation[1]);
    __try {
        auto vtable = *reinterpret_cast<const uintptr_t* const*>(object);
        using WhatFn = const char*(__thiscall*)(const void*);
        const char* what = reinterpret_cast<WhatFn>(vtable[1])(object);
        return IsBadStringPtrA(what, 256) ? "" : what;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

void log_stack(const CONTEXT* c, uintptr_t base, int max_entries) {
    const uint32_t* sp = reinterpret_cast<const uint32_t*>(c->Esp);
    int shown = 0;
    for (int i = 0; i < 1024 && shown < max_entries; i++) {
        if (IsBadReadPtr(sp + i, 4)) {
            break;
        }
        uint32_t v = sp[i];
        if (v >= base && v < base + host().exe_size) {
            logf("crash:   stack[%4d] %08X = listing %08X", i, v, static_cast<unsigned>(v - base + 0x00400000));
            shown++;
        }
    }
}

LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == kCxxException) {
        // Not fatal by itself (the game throws and catches), but when terminate() follows, the last
        // throw logged here is the cause. The first few per run, with type, message, throw site and
        // the exe return addresses above it.
        if (InterlockedIncrement(&g_throws_logged) > kMaxThrowLogs) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        auto base = reinterpret_cast<uintptr_t>(host().exe_base);
        const char* type_name = thrown_type_name(info->ExceptionRecord);
        logf("crash: C++ exception thrown: type %s what \"%s\" (thread %lu%s)", type_name,
             thrown_what(info->ExceptionRecord), GetCurrentThreadId(),
             GetCurrentThreadId() == g_main_thread ? ", game thread" : "");
        log_stack(info->ContextRecord, base, 12);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!is_fatal(code) || InterlockedCompareExchange(&g_reported, 1, 0) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code == EXCEPTION_STACK_OVERFLOW) {
        // The guard page is gone: no formatting buffers on this stack.
        host().log(&host(), "crash: stack overflow on the faulting thread (no details: the stack is exhausted)");
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
    log_stack(c, base, 24);
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
