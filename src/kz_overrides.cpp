// Killzone (SCUS-97402) runtime overrides.
//
// Bindings made here take effect at load time without regenerating the recompiled code: every guest call goes
// through PS2Runtime::dispatchGuestBranch, which looks up the function table that replaceFunction() patches.
// Use this for library functions the ps2_analyzer signature DB misses. Evidence for each binding: docs/findings.md.

#include "game_overrides.h"
#include "kz_hook_addresses.h"
#include "kz_ipu.h"
#include "kz_sif_modules.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_timeline.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
    struct Binding
    {
        uint32_t address;
        const char *handler;
    };

    // Stripped-ELF library functions identified by hand (docs/findings.md#library-bindings).
    constexpr Binding kBindings[] = {
        {kz_hook::SifInitRpc, "sceSifInitRpc"},
        {kz_hook::SifLoadModule, "sceSifLoadModule"},
        {kz_hook::SifStopModule, "SifStopModule"}, // sceSifStopModule(modid, arg_len, args, *result)
    };

    // Library functions the runtime has no handler for; implemented in kz_sif_modules.cpp.
    struct CustomBinding
    {
        uint32_t address;
        PS2Runtime::RecompiledFunction fn;
        const char *name;
    };
    // Without the IPU (KZ_IPU=off or kzipu init failure) video playback cannot work: the movie object's open method
    // (vtable 0x52EA78 +0x10, FUN_002ded00) reports failure so every caller skips its video (attract loop, cutscenes).
    void kzMovieOpenUnavailable(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        ctx->pc = getRegU32(ctx, 31);
        setReturnS32(ctx, 0);
    }

    const CustomBinding kCustom[] = {

        {kz_hook::SifSearchModuleByName, &kzSceSifSearchModuleByName, "sceSifSearchModuleByName"},
        {kz_hook::SifUnloadModule, &kzSceSifUnloadModule, "sceSifUnloadModule"},
        {kz_hook::KeyboardModuleInit, &kzLgkbmInit, "lgkbm init"},
    };

    // KZ_TIMELINE: wrappers that log entry/exit of the frame pipeline's guest functions (patch 0025). The wrapper takes the
    // function's table slot (a call, or a scheduler dispatch, enters it), logs, and calls the original. A resume at an inner
    // label goes through its own table slot and does not pass here. "Exit" is only logged for a plain return (ctx->pc == $ra).
    struct TlHook
    {
        uint32_t addr = 0;
        PS2Runtime::RecompiledFunction orig = nullptr;
    };
    template <int N>
    struct TlWrap
    {
        static inline TlHook hook;
        static uint32_t extra(const uint8_t *rdram, uint32_t ra)
        {
            auto rd = [rdram](uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & 0x01FFFFFFu), 4); return v; };
            switch (hook.addr)
            {
            case kz_hook::TimelineFrameLimiter: return rd(0x0055A6E0u);             // limiter: vsync counter
            case kz_hook::TimelineKickCheck:                                        // kick check: flag | next buffer state<<8 | current index<<16
            {
                const uint32_t cur = rd(0x0055A6B4u);
                return (rdram[0x0055A795u]) | (static_cast<uint32_t>(rdram[0x0055A6E8u + ((cur ^ 1u) & 1u)]) << 8) | ((cur & 1u) << 16);
            }
            case kz_hook::TimelineVblank: return rd(0x0055A6E0u);
            default: return ra;
            }
        }
        static void fn(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
        {
            const uint32_t ra = getRegU32(ctx, 31);
            const bool tl = ps2tl::on();
            if (tl)
                ps2tl::rec(ps2tl::G_ENTER, hook.addr, extra(rdram, ra), static_cast<uint32_t>(rt->eeScheduler().eeCycleForTrace()));
            hook.orig(rdram, ctx, rt);
            if (tl && ctx->pc == ra)
                ps2tl::rec(ps2tl::G_EXIT, hook.addr, extra(rdram, ra), static_cast<uint32_t>(rt->eeScheduler().eeCycleForTrace()));
        }
    };
    template <int N>
    void tlInstall(PS2Runtime &runtime, uint32_t addr)
    {
        TlWrap<N>::hook.addr = addr;
        TlWrap<N>::hook.orig = runtime.lookupFunction(addr);
        if (!TlWrap<N>::hook.orig || !runtime.replaceFunction(addr, &TlWrap<N>::fn))
            std::cerr << "[kz] timeline hook failed @0x" << std::hex << addr << std::dec << std::endl;
    }

    void applyKillzone(PS2Runtime &runtime)
    {
        if (const char *tl = std::getenv("KZ_TIMELINE"); tl && *tl)
        {
            tlInstall<0>(runtime, kz_hook::TimelineFrameLimiter); // frame limiter / delta
            tlInstall<1>(runtime, kz_hook::TimelineWaitD1Idle); // wait for D1 idle, then set the kick flag
            tlInstall<2>(runtime, kz_hook::TimelineWaitVblankKick); // wait until the vblank handler kicked
            tlInstall<3>(runtime, kz_hook::TimelineKick); // the kick (D1 CHCR store)
            tlInstall<4>(runtime, kz_hook::TimelineKickCheck); // vblank handler: kick check
            tlInstall<5>(runtime, kz_hook::TimelineVblank); // vblank handler
        }
        for (const Binding &b : kBindings)
        {
            if (!ps2_game_overrides::bindAddressHandler(runtime, b.address, b.handler))
                std::cerr << "[kz] failed to bind " << b.handler << " @0x" << std::hex << b.address << std::dec << std::endl;
        }
        if (!kzIpuActive() && !runtime.replaceFunction(kz_hook::MovieOpen, &kzMovieOpenUnavailable))
            std::cerr << "[kz] failed to bind movie-open skip" << std::endl;
        for (const CustomBinding &c : kCustom)
        {
            if (!runtime.replaceFunction(c.address, c.fn))
                std::cerr << "[kz] failed to bind " << c.name << " @0x" << std::hex << c.address << std::dec << std::endl;
        }
    }
}

// ELF CRC32 (zlib) of SCUS_974.02 v1.00 = 0x114E85F8 (PCSX2-style XOR CRC is CAAEC49C).
PS2_REGISTER_GAME_OVERRIDE("Killzone SCUS-97402", "SCUS_974.02", kz_hook::GameEntry, 0x114E85F8u, applyKillzone)
