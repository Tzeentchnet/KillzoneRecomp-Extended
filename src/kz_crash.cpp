#include "kz_host/core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    LONG WINAPI onUnhandled(EXCEPTION_POINTERS *ep)
    {
        const EXCEPTION_RECORD *er = ep->ExceptionRecord;
        std::fprintf(stderr, "[kz] CRASH: exception 0x%08lx at %p", er->ExceptionCode, er->ExceptionAddress);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            std::fprintf(stderr, " (%s 0x%llx)", er->ExceptionInformation[0] == 1 ? "write" : "read",
                         static_cast<unsigned long long>(er->ExceptionInformation[1]));
        std::fprintf(stderr, "\n");
        const CONTEXT &fault = *ep->ContextRecord;
        std::fprintf(stderr,
                     "[kz] regs: rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx rsi=%016llx rdi=%016llx\n"
                     "[kz]       r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx r12=%016llx r13=%016llx\n"
                     "[kz]       r14=%016llx r15=%016llx rbp=%016llx rsp=%016llx rip=%016llx\n",
                     static_cast<unsigned long long>(fault.Rax), static_cast<unsigned long long>(fault.Rbx),
                     static_cast<unsigned long long>(fault.Rcx), static_cast<unsigned long long>(fault.Rdx),
                     static_cast<unsigned long long>(fault.Rsi), static_cast<unsigned long long>(fault.Rdi),
                     static_cast<unsigned long long>(fault.R8), static_cast<unsigned long long>(fault.R9),
                     static_cast<unsigned long long>(fault.R10), static_cast<unsigned long long>(fault.R11),
                     static_cast<unsigned long long>(fault.R12), static_cast<unsigned long long>(fault.R13),
                     static_cast<unsigned long long>(fault.R14), static_cast<unsigned long long>(fault.R15),
                     static_cast<unsigned long long>(fault.Rbp), static_cast<unsigned long long>(fault.Rsp),
                     static_cast<unsigned long long>(fault.Rip));

        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(process, nullptr, TRUE);

        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512];
        for (int i = 0; i < 48; ++i)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                             SymGetModuleBase64, nullptr))
                break;
            const DWORD64 pc = frame.AddrPC.Offset;
            if (pc == 0)
                break;
            auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
            std::memset(symBuf, 0, sizeof(symBuf));
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;
            DWORD64 disp = 0;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            const bool haveSym = SymFromAddr(process, pc, &disp, sym) != FALSE;
            const bool haveLine = SymGetLineFromAddr64(process, pc, &lineDisp, &line) != FALSE;
            std::fprintf(stderr, "[kz]   #%02d %p %s+0x%llx", i, reinterpret_cast<void *>(pc), haveSym ? sym->Name : "?",
                         static_cast<unsigned long long>(disp));
            if (haveLine)
                std::fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
            std::fprintf(stderr, "\n");
        }
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

void kz::host::installCrashReporterIfRequested()
{
    const char *v = std::getenv("KZ_CRASH_TRACE");
    if (v && *v && *v != '0')
        SetUnhandledExceptionFilter(&onUnhandled);
}
