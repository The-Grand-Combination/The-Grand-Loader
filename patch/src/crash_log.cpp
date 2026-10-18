#include "crash_log.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "hooks/memory.h"
#include "log.h"

// A vectored handler, so it runs before the game's own handlers and also sees first-chance exceptions.
// Memory is probed with hooks::readable (VirtualQuery), never by touching it: an access violation raised
// while logging would re-enter the handler and use the one fatal report on a fake crash. A per-thread
// depth guard covers anything else that faults inside it.

namespace cp {

namespace {

constexpr DWORD kCxxException = 0xE06D7363;  // MSVC C++ throw
constexpr DWORD kFatalAppExit = 0x40000015;  // abort()
constexpr int kMaxThrowLogs = 12;
constexpr int kThrowStackEntries = 12;
constexpr int kCrashStackEntries = 24;
constexpr int kStackScanDwords = 1024;
constexpr int kMaxCatchableTypes = 64;
constexpr int kMaxContexts = 4;
constexpr size_t kTypeNameProbe = 32;   // bytes of a type descriptor read for its name
constexpr UINT_PTR kWhatProbe = 256;    // characters of what() checked

CrashContextFn g_contexts[kMaxContexts];
int g_context_count = 0;
LONG g_reported = 0;
LONG g_throws_logged = 0;
DWORD g_main_thread = 0;  // the thread that installed the handler: the game's main thread (plugin init)
thread_local int t_depth = 0;

const char* code_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case kFatalAppExit:
            return "abort() (STATUS_FATAL_APP_EXIT: terminate() after an uncaught C++ exception, or a CRT assert)";
        default: return "exception";
    }
}

bool is_fatal(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
           code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
           code == EXCEPTION_PRIV_INSTRUCTION || code == EXCEPTION_IN_PAGE_ERROR || code == kFatalAppExit;
}

const char* thread_label() { return GetCurrentThreadId() == g_main_thread ? "game thread" : "other thread"; }

bool in_exe(uintptr_t address) {
    uintptr_t base = hooks::exe_base();
    return address >= base && address < base + host().exe_size;
}

// MSVC x86 RTTI for a thrown object: ExceptionInformation[2] is the _ThrowInfo.
struct TypeDescriptor { const void* vftable; void* spare; char name[1]; };
struct CatchableType { uint32_t properties; TypeDescriptor* type; };
struct CatchableTypeArray { int count; CatchableType* types[1]; };
struct ThrowInfo { uint32_t attributes; void* unwind; void* forward_compat; CatchableTypeArray* catchable; };

// The catchable types of a thrown object (every base it can be caught as), or 0 of them.
int catchable_types(const EXCEPTION_RECORD* r, CatchableType* const** types) {
    if (r->NumberParameters < 3) {
        return 0;
    }
    const auto* ti = reinterpret_cast<const ThrowInfo*>(r->ExceptionInformation[2]);
    if (!hooks::readable(ti, sizeof(*ti)) || !hooks::readable(ti->catchable, sizeof(CatchableTypeArray))) {
        return 0;
    }
    int count = ti->catchable->count;
    if (count < 1 || count > kMaxCatchableTypes ||
        !hooks::readable(ti->catchable->types, static_cast<size_t>(count) * sizeof(CatchableType*))) {
        return 0;
    }
    *types = ti->catchable->types;
    return count;
}

const char* type_name(const CatchableType* type) {
    if (!hooks::readable(type, sizeof(*type)) || !hooks::readable(type->type, kTypeNameProbe)) {
        return nullptr;
    }
    return type->type->name;  // e.g. ".?AVbad_alloc@std@@"
}

// std::exception::what() is vtable slot 1 on MSVC; called only for objects catchable as std::exception.
const char* thrown_what(const EXCEPTION_RECORD* r, CatchableType* const* types, int count) {
    bool is_std_exception = false;
    for (int i = 0; i < count && !is_std_exception; i++) {
        const char* name = type_name(types[i]);
        is_std_exception = name && std::strcmp(name, ".?AVexception@std@@") == 0;
    }
    if (!is_std_exception) {
        return "";
    }
    const void* object = reinterpret_cast<const void*>(r->ExceptionInformation[1]);
    __try {
        auto vtable = *reinterpret_cast<const uintptr_t* const*>(object);
        using WhatFn = const char*(__thiscall*)(const void*);
        const char* what = reinterpret_cast<WhatFn>(vtable[1])(object);
        return hooks::readable(what, 1) && !IsBadStringPtrA(what, kWhatProbe) ? what : "";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

void log_stack(const CONTEXT* c, int max_entries) {
    const auto* sp = reinterpret_cast<const uint32_t*>(c->Esp);
    int shown = 0;
    for (int i = 0; i < kStackScanDwords && shown < max_entries && hooks::readable(sp + i, 4); i++) {
        if (in_exe(sp[i])) {
            logf("crash:   stack[%4d] %08X = listing %08X", i, sp[i], hooks::listing_va_of(sp[i]));
            shown++;
        }
    }
}

// Not fatal by itself (the game throws and catches), but when terminate() follows, the last throw logged
// here is the cause. The first few per run, with type, message, throw site and the exe return addresses
// above it.
void log_cxx_throw(const EXCEPTION_POINTERS* info) {
    if (InterlockedIncrement(&g_throws_logged) > kMaxThrowLogs) {
        return;
    }
    CatchableType* const* types = nullptr;
    int count = catchable_types(info->ExceptionRecord, &types);
    const char* name = count > 0 ? type_name(types[0]) : nullptr;
    logf("crash: C++ exception thrown: type %s what \"%s\" (thread %lu, %s)", name ? name : "?",
         thrown_what(info->ExceptionRecord, types, count), GetCurrentThreadId(), thread_label());
    log_stack(info->ContextRecord, kThrowStackEntries);
}

// Exe addresses as listing VAs (base 0x00400000, as in docs/re/decomp/), others as module+offset
// (performance_patch.dll offsets match its link map).
void log_fault_location(DWORD code, uintptr_t eip) {
    if (in_exe(eip)) {
        logf("crash: %s (0x%08lX) at %08X = v2game.exe listing %08X", code_name(code), code, eip,
             hooks::listing_va_of(eip));
        return;
    }
    HMODULE module = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(eip), &module) && module) {
        GetModuleFileNameA(module, path, MAX_PATH);
    }
    const char* name = path;
    for (const char* p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            name = p + 1;
        }
    }
    logf("crash: %s (0x%08lX) at %08X = %s+%X", code_name(code), code, eip, name,
         static_cast<unsigned>(eip - reinterpret_cast<uintptr_t>(module)));
}

void log_fatal(const EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_STACK_OVERFLOW) {
        // The guard page is gone: no formatting buffers on this stack.
        host().log(&host(), "crash: stack overflow on the faulting thread (no details: the stack is exhausted)");
        return;
    }
    const CONTEXT* c = info->ContextRecord;
    log_fault_location(code, c->Eip);
    logf("crash:   thread %lu (%s)", GetCurrentThreadId(), thread_label());
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
        logf("crash:   %s address %08X", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
             static_cast<unsigned>(info->ExceptionRecord->ExceptionInformation[1]));
    }
    logf("crash:   eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X", c->Eax, c->Ebx,
         c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    log_stack(c, kCrashStackEntries);
    for (int i = 0; i < g_context_count; i++) {
        g_contexts[i]();
    }
}

void handle(const EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == kCxxException) {
        log_cxx_throw(info);
    } else if (is_fatal(code) && InterlockedCompareExchange(&g_reported, 1, 0) == 0) {
        log_fatal(info);
    }
}

LONG CALLBACK handler(EXCEPTION_POINTERS* info) {
    if (t_depth == 0) {
        t_depth++;
        handle(info);
        t_depth--;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install_crash_log() {
    g_main_thread = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, handler);
}

void add_crash_context(CrashContextFn context) {
    if (context && g_context_count < kMaxContexts) {
        g_contexts[g_context_count++] = context;
    }
}

}  // namespace cp
