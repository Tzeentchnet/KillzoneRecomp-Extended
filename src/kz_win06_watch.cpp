// WIN-06 diagnostic, KZ_WIN06_WATCH=1: a page-protection write watch on libkernel's Timer 2 alarm state block
// (0x517748-0x517780) and its node pools (0x56CB40-0x56EF50) in the host copy of EE RAM.
//
// The host pages that hold those guest ranges are made read-only. A write fault on one of them saves the faulting
// thread's CONTEXT and a snapshot of the watched bytes on that page, unprotects the page and single-steps the
// instruction (trap flag). The single-step trap re-protects the page and diffs the snapshot, so only stores that
// changed a watched word are reported (writes to neighbouring data on the same pages are only counted). Each change is
// attributed from the host stack, symbolised from the PDB: generated functions are named after their guest address and
// their source lines carry the guest pc as comments, so a write is classed as
//   timer  - guest code inside libkernel's timer code (0x2B6B50-0x2B7F00), expected;
//   guest  - any other generated (guest) code;
//   host   - runtime/host code (DMA, VIF, SIF, IPU, HLE stubs, ...).
// The first KZ_WIN06_WATCH_EXPECTED (default 200) timer writes are logged, later ones only counted; every guest and
// host write is logged, with the full host stack for the first few per faulting instruction. A validator checks after
// each change (and every millisecond from a helper thread, which also catches writes that bypass the fault, such as
// kernel-mode ReadFile) that the alarm list heads and links point at pool nodes, and prints the last writes when they
// do not. The same flag hooks the game heap (free, realloc and its C wrapper) and the resource upload path to report
// corrupt block pointers and upload records at their source (see "guest hooks" below). Output goes to stderr,
// prefixed "[win06]".

#include "kz_win06_watch.h"

#include "kz_hook_addresses.h"

#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    struct GuestRange
    {
        uint32_t lo, hi;
    };
    constexpr GuestRange kRanges[] = {{0x517748u, 0x517780u}, {0x56CB40u, 0x56EF50u}};
    constexpr uint32_t kTimerCodeLo = 0x2B6B50u, kTimerCodeHi = 0x2B7F00u;
    constexpr uint32_t kListHead = 0x517760u, kFreeHead = 0x51775Cu;
    constexpr uint32_t kPoolLo = 0x56CB40u, kPoolHi = 0x56EB40u, kNodeSize = 0x40u;
    constexpr size_t kPageSize = 0x1000;

    enum WriterClass
    {
        kTimer,
        kGuest,
        kHost
    };
    const char *const kClassName[] = {"timer", "guest", "host"};

    struct Page
    {
        uintptr_t host = 0;
        std::vector<GuestRange> watched; // guest ranges on this page
        int unprotected = 0;             // threads currently single-stepping a store on it
    };

    struct Pending
    {
        int page = -1;
        uint8_t snapshot[kPageSize];
    };

    struct ThreadState
    {
        int depth = 0;
        Pending pending[4];
        CONTEXT context; // at the first fault of the instruction
        uintptr_t faultAddress = 0;
    };
    thread_local ThreadState t_state;

    struct SiteInfo
    {
        std::string symbol, file;
        unsigned long line = 0;
        uint32_t guestPc = 0;
        WriterClass cls = kHost;
        int stacksPrinted = 0;
    };

    struct Change
    {
        uint64_t seq, vsync;
        uint32_t address, before, after;
        DWORD64 rip;
        DWORD tid;
        WriterClass cls;
    };

    PS2Runtime *g_runtime = nullptr;
    uint8_t *g_rdram = nullptr;
    std::vector<Page> g_pages;
    CRITICAL_SECTION g_protectLock;
    std::mutex g_reportLock;
    std::unordered_map<DWORD64, SiteInfo> g_sites;
    Change g_ring[64];
    uint64_t g_seq = 0;
    std::atomic<DWORD> g_eeThread{0};
    std::atomic<uint64_t> g_faults{0}, g_changes{0}, g_byClass[3];
    std::atomic<bool> g_invalidReported{false}, g_crashReported{false};
    uint64_t g_expectedLimit = 200;
    ULONGLONG g_startMs = 0;

    uint32_t rd32(uint32_t address) { return *reinterpret_cast<const uint32_t *>(g_rdram + address); }

    uint64_t vsyncTick() { return g_runtime->memory().gs().vsyncTick.load(std::memory_order_relaxed); }

    double seconds() { return static_cast<double>(GetTickCount64() - g_startMs) / 1000.0; }

    bool validNode(uint32_t p)
    {
        // InitTimer (0x2B6B70) links the free nodes upwards and briefly points the last one at the pool end before
        // clearing it (0x2B6C04), so the end address is accepted as a terminator.
        p &= 0x1FFFFFFFu;
        return p == 0 || p == kPoolHi || (p >= kPoolLo && p < kPoolHi && (p - kPoolLo) % kNodeSize == 0);
    }

    // Empty when the alarm lists look sane, else a description of the first bad link.
    std::string checkLists()
    {
        char buf[160];
        for (const auto [name, head] : {std::pair{"active", kListHead}, std::pair{"free", kFreeHead}})
        {
            uint32_t p = rd32(head), at = head;
            for (int n = 0; p && (p & 0x1FFFFFFFu) != kPoolHi && n < 160; ++n)
            {
                if (!validNode(p))
                {
                    std::snprintf(buf, sizeof(buf), "%s list: word 0x%06x holds 0x%08x, not a pool node", name, at, p);
                    return buf;
                }
                at = (p & 0x1FFFFFFFu);
                p = rd32(at);
            }
        }
        return {};
    }

    int findPage(uintptr_t address)
    {
        const uintptr_t base = address & ~(kPageSize - 1);
        for (size_t i = 0; i < g_pages.size(); ++i)
            if (g_pages[i].host == base)
                return static_cast<int>(i);
        return -1;
    }

    void setProtection(const Page &page, DWORD protection)
    {
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void *>(page.host), kPageSize, protection, &old);
    }

    // Guest pc of a generated source line: the closest "// 0x<pc>:" comment at or above it.
    uint32_t guestPcFromSource(const std::string &file, unsigned long line)
    {
        if (file.find("FUN_") == std::string::npos && file.find("entry_") == std::string::npos &&
            file.find("thunk_") == std::string::npos)
            return 0;
        std::ifstream in(file);
        std::string text;
        uint32_t pc = 0;
        for (unsigned long n = 1; n <= line && std::getline(in, text); ++n)
        {
            const size_t at = text.find("// 0x");
            unsigned value = 0;
            if (at != std::string::npos && text.find(':', at) != std::string::npos &&
                std::sscanf(text.c_str() + at, "// 0x%x:", &value) == 1)
                pc = value;
        }
        return pc;
    }

    // Last "_0x<hex>" in a generated function name (FUN_002b6f80_0x2b6f80, entry_2b6db0_0x2b6f80).
    uint32_t guestAddressFromName(const std::string &name)
    {
        const size_t at = name.rfind("_0x");
        if (at == std::string::npos)
            return 0;
        return static_cast<uint32_t>(std::strtoul(name.c_str() + at + 3, nullptr, 16));
    }

    bool isGeneratedName(const std::string &name)
    {
        return name.rfind("FUN_", 0) == 0 || name.rfind("entry_", 0) == 0 || name.rfind("thunk_", 0) == 0;
    }

    SiteInfo &site(DWORD64 rip)
    {
        auto it = g_sites.find(rip);
        if (it != g_sites.end())
            return it->second;
        SiteInfo info;
        HANDLE process = GetCurrentProcess();
        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        char text[600];
        if (SymFromAddr(process, rip, &disp, sym))
        {
            std::snprintf(text, sizeof(text), "%s+0x%llx", sym->Name, static_cast<unsigned long long>(disp));
            info.symbol = text;
        }
        else
        {
            std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(rip));
            info.symbol = text;
        }
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        if (SymGetLineFromAddr64(process, rip, &lineDisp, &line))
        {
            info.file = line.FileName;
            info.line = line.LineNumber;
            info.guestPc = guestPcFromSource(info.file, info.line);
        }
        const std::string name = info.symbol.substr(0, info.symbol.find('+'));
        uint32_t guestAddress = info.guestPc;
        if (!guestAddress && isGeneratedName(name))
            guestAddress = guestAddressFromName(name);
        if (guestAddress)
            info.cls = guestAddress >= kTimerCodeLo && guestAddress < kTimerCodeHi ? kTimer : kGuest;
        else
            info.cls = isGeneratedName(name) ? kGuest : kHost;
        return g_sites.emplace(rip, std::move(info)).first->second;
    }

    void printStack(const CONTEXT &start)
    {
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        CONTEXT ctx = start;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 24; ++i)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                             SymGetModuleBase64, nullptr) ||
                frame.AddrPC.Offset == 0)
                break;
            const SiteInfo &s = site(frame.AddrPC.Offset);
            std::fprintf(stderr, "[win06]     #%02d %s", i, s.symbol.c_str());
            if (s.guestPc)
                std::fprintf(stderr, " [guest pc 0x%06x]", s.guestPc);
            if (!s.file.empty())
                std::fprintf(stderr, " (%s:%lu)", s.file.c_str(), s.line);
            std::fprintf(stderr, "\n");
        }
    }

    void printRing(const char *why)
    {
        std::fprintf(stderr, "[win06] last watched writes (%s):\n", why);
        const uint64_t first = g_seq > 64 ? g_seq - 64 : 0;
        for (uint64_t s = first; s < g_seq; ++s)
        {
            const Change &c = g_ring[s % 64];
            const SiteInfo &info = site(c.rip);
            std::fprintf(stderr, "[win06]   #%llu vsync %llu tid %lu %s 0x%06x 0x%08x -> 0x%08x by %s", static_cast<unsigned long long>(c.seq),
                         static_cast<unsigned long long>(c.vsync), c.tid, kClassName[c.cls], c.address, c.before, c.after,
                         info.symbol.c_str());
            if (info.guestPc)
                std::fprintf(stderr, " [guest pc 0x%06x]", info.guestPc);
            std::fprintf(stderr, "\n");
        }
        std::fflush(stderr);
    }

    void reportInvalid(const std::string &problem, const char *where)
    {
        if (g_invalidReported.exchange(true))
            return;
        std::fprintf(stderr, "[win06] INVALID alarm list at %.3f s, vsync %llu (%s): %s\n", seconds(),
                     static_cast<unsigned long long>(vsyncTick()), where, problem.c_str());
        printRing("before the invalid list");
    }

    // Called on the single-step trap after a store to a watched page, with the page re-protected.
    void report(const ThreadState &state, const Pending &pending)
    {
        const Page &page = g_pages[pending.page];
        const uint8_t *now = reinterpret_cast<const uint8_t *>(page.host);
        const uint32_t pageGuest = static_cast<uint32_t>(page.host - reinterpret_cast<uintptr_t>(g_rdram));
        // aligned guest words whose bytes changed
        uint32_t changed[8], before[8], after[8];
        int count = 0, total = 0;
        for (const GuestRange &r : page.watched)
        {
            for (uint32_t a = r.lo & ~3u; a < r.hi; a += 4)
            {
                const uint32_t off = a - pageGuest;
                if (off + 4 > kPageSize || a < pageGuest)
                    continue;
                uint32_t b, n;
                std::memcpy(&b, pending.snapshot + off, 4);
                std::memcpy(&n, now + off, 4);
                if (b == n)
                    continue;
                if (count < 8)
                {
                    changed[count] = a;
                    before[count] = b;
                    after[count] = n;
                    ++count;
                }
                ++total;
            }
        }
        if (!total)
            return;

        std::lock_guard<std::mutex> lock(g_reportLock);
        SiteInfo &info = site(state.context.Rip);
        DWORD expectedEe = 0;
        if (info.cls != kHost) // all guest code runs on the runtime's one executor thread
            g_eeThread.compare_exchange_strong(expectedEe, GetCurrentThreadId());
        const uint64_t index = g_changes.fetch_add(1);
        const uint64_t classIndex = g_byClass[info.cls].fetch_add(1);
        const uint64_t vsync = vsyncTick();
        for (int i = 0; i < count; ++i)
        {
            g_ring[g_seq % 64] = Change{index, vsync, changed[i], before[i], after[i], state.context.Rip,
                                        GetCurrentThreadId(), info.cls};
            ++g_seq;
        }

        const bool log = info.cls != kTimer || classIndex < g_expectedLimit;
        if (log)
        {
            std::fprintf(stderr, "[win06] %s write #%llu at %.3f s vsync %llu tid %lu:", kClassName[info.cls],
                         static_cast<unsigned long long>(index), seconds(), static_cast<unsigned long long>(vsync),
                         GetCurrentThreadId());
            for (int i = 0; i < count; ++i)
                std::fprintf(stderr, " 0x%06x 0x%08x->0x%08x", changed[i], before[i], after[i]);
            if (total > count)
                std::fprintf(stderr, " (+%d words)", total - count);
            std::fprintf(stderr, " by %s", info.symbol.c_str());
            if (info.guestPc)
                std::fprintf(stderr, " [guest pc 0x%06x]", info.guestPc);
            if (GetCurrentThreadId() == g_eeThread.load())
            {
                EeScheduler &scheduler = g_runtime->eeScheduler();
                if (const R5900Context *ctx = scheduler.currentContext())
                    std::fprintf(stderr, " ee thread %d ctx.pc 0x%08x ra 0x%08x", scheduler.currentThreadId(), ctx->pc,
                                 static_cast<uint32_t>(_mm_cvtsi128_si32(ctx->r[31])));
            }
            else
                std::fprintf(stderr, " (not the EE thread)");
            std::fprintf(stderr, " fault 0x%llx\n", static_cast<unsigned long long>(state.faultAddress -
                                                                                    reinterpret_cast<uintptr_t>(g_rdram)));
            if (classIndex + 1 == g_expectedLimit && info.cls == kTimer)
                std::fprintf(stderr, "[win06] %llu timer writes logged; further ones are only counted\n",
                             static_cast<unsigned long long>(g_expectedLimit));
            if (info.cls != kTimer)
            {
                // The slow store path masks addresses above 32 MB onto RAM; host registers that alias the written
                // offset show the unmasked guest address.
                const uint32_t offset = static_cast<uint32_t>(state.faultAddress - reinterpret_cast<uintptr_t>(g_rdram));
                const DWORD64 regs[] = {state.context.Rax, state.context.Rbx, state.context.Rcx, state.context.Rdx,
                                        state.context.Rsi, state.context.Rdi, state.context.R8,  state.context.R9,
                                        state.context.R10, state.context.R11, state.context.R12, state.context.R13,
                                        state.context.R14, state.context.R15};
                const char *const regNames[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "r8",
                                                "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
                std::fprintf(stderr, "[win06]   host regs aliasing 0x%06x:", offset);
                for (int i = 0; i < 14; ++i)
                {
                    const uint32_t lo = static_cast<uint32_t>(regs[i]);
                    if ((lo & 0x1FFFFF0u) == (offset & 0x1FFFFF0u) && lo != offset)
                        std::fprintf(stderr, " %s=0x%08x", regNames[i], lo);
                }
                std::fprintf(stderr, "\n");
                if (GetCurrentThreadId() == g_eeThread.load())
                    if (const R5900Context *ctx = g_runtime->eeScheduler().currentContext())
                    {
                        // guest registers as last flushed to the context (generated code keeps live ones in locals)
                        std::fprintf(stderr, "[win06]   ctx gprs:");
                        for (int r : {2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 29, 31})
                            std::fprintf(stderr, " r%d=0x%08x", r, static_cast<uint32_t>(_mm_cvtsi128_si32(ctx->r[r])));
                        std::fprintf(stderr, "\n");
                    }
                // game heap descriptor 0x55EC20 (dlmalloc-style): top chunk +0x30, bounds +0x448/+0x44C
                std::fprintf(stderr, "[win06]   heap 0x55ec20: top 0x%08x bounds 0x%08x-0x%08x\n", rd32(0x55EC50u),
                             rd32(0x55F068u), rd32(0x55F06Cu));
                if (info.stacksPrinted < 3)
                {
                    ++info.stacksPrinted;
                    printStack(state.context);
                }
            }
            std::fflush(stderr);
        }
        const std::string problem = checkLists();
        if (!problem.empty())
            reportInvalid(problem, "after a watched write");
    }

    LONG CALLBACK onException(EXCEPTION_POINTERS *ep)
    {
        const EXCEPTION_RECORD *er = ep->ExceptionRecord;
        ThreadState &state = t_state;
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2 &&
            er->ExceptionInformation[0] == 1)
        {
            const uintptr_t address = static_cast<uintptr_t>(er->ExceptionInformation[1]);
            const int index = findPage(address);
            if (index >= 0 && state.depth < 4)
            {
                g_faults.fetch_add(1, std::memory_order_relaxed);
                Page &page = g_pages[index];
                Pending &pending = state.pending[state.depth];
                if (state.depth == 0)
                {
                    state.context = *ep->ContextRecord;
                    state.faultAddress = address;
                }
                pending.page = index;
                std::memcpy(pending.snapshot, reinterpret_cast<const void *>(page.host), kPageSize);
                ++state.depth;
                EnterCriticalSection(&g_protectLock);
                if (page.unprotected++ == 0)
                    setProtection(page, PAGE_READWRITE);
                LeaveCriticalSection(&g_protectLock);
                ep->ContextRecord->EFlags |= 0x100; // trap after the store
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        else if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && state.depth > 0)
        {
            EnterCriticalSection(&g_protectLock);
            for (int i = 0; i < state.depth; ++i)
            {
                Page &page = g_pages[state.pending[i].page];
                if (--page.unprotected == 0)
                    setProtection(page, PAGE_READONLY);
            }
            LeaveCriticalSection(&g_protectLock);
            const int depth = state.depth;
            state.depth = 0;
            for (int i = 0; i < depth; ++i)
                report(state, state.pending[i]);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !g_crashReported.exchange(true))
        {
            std::unique_lock<std::mutex> lock(g_reportLock, std::try_to_lock);
            if (!lock.owns_lock())
                return EXCEPTION_CONTINUE_SEARCH; // fault inside the reporter itself
            std::fprintf(stderr, "[win06] first unrelated access violation at %.3f s, vsync %llu, host pc %p\n", seconds(),
                         static_cast<unsigned long long>(vsyncTick()), er->ExceptionAddress);
            const std::string problem = checkLists();
            std::fprintf(stderr, "[win06] alarm lists: %s\n", problem.empty() ? "valid" : problem.c_str());
            printRing("before the access violation");
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ---- guest hooks: the game heap and the resource upload path ----
    // free 0x172DD0, realloc 0x173038 and the C realloc wrapper 0x173660 (NULL -> malloc 0x1727F8, size 0 -> free)
    // share one dlmalloc-style heap: descriptor 0x55EC20, bounds +0x448/+0x44C, chunk header at ptr-8 with the size
    // (bit 0 = previous chunk in use) at ptr-4. A pointer passed to them must have a header that fits the heap and,
    // for chunks above the small-bin limit (0x50), a next chunk that marks it in use. Results cannot be checked after
    // calling the original: generated functions often return through tail-jump fragments that the call site follows.
    // 0x15A268 (called from the upload walker 0x15A5E8) passes (records a0, count a1) to the packet builder 0x159D30;
    // each record is {address, qword count}. A record that does not describe guest RAM is reported with the walker's
    // object (s1).
    using Fn = PS2Runtime::RecompiledFunction;
    Fn g_origFree = nullptr, g_origRealloc = nullptr, g_origUpload = nullptr, g_origReallocC = nullptr;

    struct FreeRecord
    {
        uint64_t vsync = 0;
        uint32_t ra = 0;
    };
    std::unordered_map<uint32_t, FreeRecord> g_frees; // last free of each pointer (it may have been reused since)
    int g_badUploads = 0, g_badBlocks = 0;

    uint32_t reg(const R5900Context *ctx, int r) { return static_cast<uint32_t>(_mm_cvtsi128_si32(ctx->r[r])); }

    bool inRam(uint32_t a, uint32_t bytes = 4) { return a < PS2_RAM_SIZE && bytes <= PS2_RAM_SIZE - a; }

    uint32_t chunkSize(uint32_t p) { return inRam(p - 4) ? (rd32(p - 4) & 0x0FFFFFFEu) : 0; }

    std::string lastFree(uint32_t p)
    {
        const auto it = g_frees.find(p);
        if (it == g_frees.end())
            return "never freed before";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "last freed at vsync %llu by ra 0x%06x",
                      static_cast<unsigned long long>(it->second.vsync), it->second.ra);
        return buf;
    }

    // A block passed to realloc/free whose chunk header does not fit the heap, or that is already free.
    void checkBlockArgument(const char *what, const R5900Context *ctx)
    {
        const uint32_t p = reg(ctx, 4), ra = reg(ctx, 31);
        if (!p)
            return;
        const uint32_t size = chunkSize(p), heapLo = rd32(0x55F068u), heapHi = rd32(0x55F06Cu);
        const bool fits = p >= heapLo && p < heapHi && size >= 0x10 && p - 8 + size <= heapHi + 0x10u;
        const bool inUse = !fits || size < 0x51 || !inRam(p - 8 + size + 4) || (rd32(p - 8 + size + 4) & 1u);
        if (fits && inUse)
            return;
        if (++g_badBlocks > 12)
            return;
        std::lock_guard<std::mutex> lock(g_reportLock);
        std::fprintf(stderr, "[win06] BAD %s(0x%08x, 0x%08x) at vsync %llu by ra 0x%06x: chunk size 0x%x, %s; %s\n", what, p,
                     reg(ctx, 5), static_cast<unsigned long long>(vsyncTick()), ra, size,
                     !fits ? "header does not fit the heap" : "already free (double free or use after free)",
                     lastFree(p).c_str());
        if (inRam(p - 0x10, 0x30))
        {
            std::fprintf(stderr, "[win06]   words at ptr-0x10:");
            for (uint32_t o = 0; o < 0x30; o += 4)
                std::fprintf(stderr, " %08x", rd32(p - 0x10 + o));
            std::fprintf(stderr, "\n");
        }
        const uint32_t sp = reg(ctx, 29);
        if (inRam(sp, 0x100))
        {
            // the callers' frames: their return addresses show up among these
            std::fprintf(stderr, "[win06]   guest stack at sp 0x%08x:", sp);
            for (uint32_t o = 0; o < 0x100; o += 4)
                std::fprintf(stderr, " %08x", rd32(sp + o));
            std::fprintf(stderr, "\n");
        }
        std::fprintf(stderr, "[win06]   ctx gprs:");
        for (int r : {2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23, 29, 30, 31})
            std::fprintf(stderr, " r%d=0x%08x", r, reg(ctx, r));
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }

    void hookRealloc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        checkBlockArgument("realloc", ctx);
        g_origRealloc(rdram, ctx, rt);
    }

    // its ra is the code that holds the pointer
    void hookReallocC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (reg(ctx, 5))
            checkBlockArgument("realloc wrapper", ctx);
        g_origReallocC(rdram, ctx, rt);
    }

    void hookFree(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        checkBlockArgument("free", ctx);
        if (const uint32_t p = reg(ctx, 4))
            g_frees[p] = FreeRecord{vsyncTick(), reg(ctx, 31)};
        g_origFree(rdram, ctx, rt);
    }

    void hookUpload(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t records = reg(ctx, 4);
        const int32_t count = static_cast<int32_t>(reg(ctx, 5));
        bool bad = count < 0 || count > 4096 || !inRam(records, static_cast<uint32_t>(count > 0 ? count : 0) * 8u);
        for (int32_t i = 0; !bad && i < count && i < 64; ++i)
        {
            const uint32_t address = rd32(records + 8u * i) & 0x1FFFFFFFu;
            const int32_t qwc = static_cast<int32_t>(rd32(records + 8u * i + 4u));
            const bool scratchpad = (rd32(records + 8u * i) & 0xF0000000u) == 0x70000000u;
            bad = qwc < 0 || qwc > 0x40000 || (!scratchpad && !inRam(address, static_cast<uint32_t>(qwc) * 16u));
        }
        if (bad && ++g_badUploads <= 3)
        {
            std::lock_guard<std::mutex> lock(g_reportLock);
            const uint32_t object = reg(ctx, 17), sub = object + reg(ctx, 18) + 0x20u;
            std::fprintf(stderr, "[win06] BAD upload record at vsync %llu: records 0x%08x count %d, ra 0x%06x\n",
                         static_cast<unsigned long long>(vsyncTick()), records, count, reg(ctx, 31));
            for (int32_t i = 0; i < 4 && inRam(records + 8u * i, 8); ++i)
                std::fprintf(stderr, "[win06]   record %d: 0x%08x qwc 0x%08x\n", i, rd32(records + 8u * i),
                             rd32(records + 8u * i + 4u));
            std::fprintf(stderr, "[win06]   records: %s\n", lastFree(records).c_str());
            if (inRam(object, 0x40))
            {
                std::fprintf(stderr, "[win06]   walker object s1 0x%08x (%s):", object, lastFree(object).c_str());
                for (uint32_t o = 0; o < 0x40; o += 4)
                    std::fprintf(stderr, " %08x", rd32(object + o));
                std::fprintf(stderr, "\n");
            }
            if (inRam(sub, 0x18))
            {
                std::fprintf(stderr, "[win06]   sub-record 0x%08x:", sub);
                for (uint32_t o = 0; o < 0x18; o += 4)
                    std::fprintf(stderr, " %08x", rd32(sub + o));
                std::fprintf(stderr, "\n");
            }
            std::fprintf(stderr, "[win06]   ctx gprs:");
            for (int r : {4, 5, 6, 7, 8, 9, 10, 16, 17, 18, 19, 20, 21, 22, 23, 29, 30, 31})
                std::fprintf(stderr, " r%d=0x%08x", r, reg(ctx, r));
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
        g_origUpload(rdram, ctx, rt);
    }

    bool hook(PS2Runtime &rt, uint32_t address, Fn fn, Fn *orig)
    {
        *orig = rt.hasFunction(address) ? rt.lookupFunction(address) : nullptr;
        return *orig && rt.replaceFunction(address, fn);
    }

    void monitor()
    {
        ULONGLONG nextSummary = GetTickCount64() + 5000;
        for (;;)
        {
            Sleep(1);
            if (!g_invalidReported.load())
            {
                const std::string problem = checkLists();
                if (!problem.empty())
                {
                    // re-check a little later so a list edit in progress is not reported
                    Sleep(2);
                    const std::string again = checkLists();
                    if (!again.empty())
                    {
                        std::lock_guard<std::mutex> lock(g_reportLock);
                        reportInvalid(again, "validator thread");
                    }
                }
            }
            if (GetTickCount64() >= nextSummary)
            {
                nextSummary += 5000;
                std::fprintf(stderr,
                             "[win06] %.0f s: %llu faults on watched pages, %llu watched changes (timer %llu, guest %llu, host %llu)\n",
                             seconds(), static_cast<unsigned long long>(g_faults.load()),
                             static_cast<unsigned long long>(g_changes.load()),
                             static_cast<unsigned long long>(g_byClass[kTimer].load()),
                             static_cast<unsigned long long>(g_byClass[kGuest].load()),
                             static_cast<unsigned long long>(g_byClass[kHost].load()));
                std::fflush(stderr);
            }
        }
    }
}

void kzWin06WatchInstall(PS2Runtime &runtime)
{
    const char *v = std::getenv("KZ_WIN06_WATCH");
    if (!v || !*v || *v == '0')
        return;
    if (const char *n = std::getenv("KZ_WIN06_WATCH_EXPECTED"))
        g_expectedLimit = std::strtoull(n, nullptr, 10);
    g_runtime = &runtime;
    g_rdram = runtime.memory().getRDRAM();
    g_startMs = GetTickCount64();
    InitializeCriticalSection(&g_protectLock);

    for (const GuestRange &r : kRanges)
    {
        const uintptr_t first = reinterpret_cast<uintptr_t>(g_rdram + r.lo) & ~(kPageSize - 1);
        const uintptr_t last = reinterpret_cast<uintptr_t>(g_rdram + r.hi - 1) & ~(kPageSize - 1);
        for (uintptr_t host = first; host <= last; host += kPageSize)
        {
            int index = findPage(host);
            if (index < 0)
            {
                g_pages.push_back(Page{host, {}, 0});
                index = static_cast<int>(g_pages.size() - 1);
            }
            const uint32_t pageGuest = static_cast<uint32_t>(host - reinterpret_cast<uintptr_t>(g_rdram));
            const uint32_t lo = std::max(r.lo, pageGuest), hi = std::min(r.hi, pageGuest + static_cast<uint32_t>(kPageSize));
            g_pages[index].watched.push_back(GuestRange{lo, hi});
        }
    }

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    {
        // load the PDB now rather than inside the first fault
        std::lock_guard<std::mutex> lock(g_reportLock);
        site(reinterpret_cast<DWORD64>(&kzWin06WatchInstall));
    }

    const bool hooked = hook(runtime, kz_hook::HeapFree, &hookFree, &g_origFree) &&
                        hook(runtime, kz_hook::HeapRealloc, &hookRealloc, &g_origRealloc) &&
                        hook(runtime, kz_hook::UploadRecords, &hookUpload, &g_origUpload) &&
                        hook(runtime, kz_hook::HeapReallocC, &hookReallocC, &g_origReallocC);
    std::fprintf(stderr, "[win06] heap and upload hooks %s\n", hooked ? "installed" : "NOT installed");

    AddVectoredExceptionHandler(1, &onException);
    for (const Page &page : g_pages)
        setProtection(page, PAGE_READONLY);
    std::fprintf(stderr, "[win06] watch on %zu host pages (rdram %p):", g_pages.size(), static_cast<void *>(g_rdram));
    for (const Page &page : g_pages)
        for (const GuestRange &r : page.watched)
            std::fprintf(stderr, " 0x%06x-0x%06x", r.lo, r.hi);
    std::fprintf(stderr, "; timer code 0x%06x-0x%06x expected, first %llu logged\n", kTimerCodeLo, kTimerCodeHi,
                 static_cast<unsigned long long>(g_expectedLimit));
    std::thread(monitor).detach();
}
