#include "kz_region.h"

#include "kz_addrs.h"
#include "kz_pal_table.h"

#include "ps2_runtime.h"

#include <cstring>
#include <iostream>
#include <sstream>

namespace
{
    KzRegion g_region = KzRegion::Us;
    bool g_selected = false;

    using TableRegisterFunction = void (*)(PS2Runtime::RecompiledFunction *, uint32_t, uint32_t);

    bool tableComplete(const KzAddrs &a)
    {
        static_assert(sizeof(KzAddrs) % sizeof(uint32_t) == 0, "KzAddrs holds only uint32_t fields");
        uint32_t w[sizeof(KzAddrs) / sizeof(uint32_t)];
        std::memcpy(w, &a, sizeof(w));
        for (uint32_t v : w)
            if (v == 0u)
                return false;
        return true;
    }

    const KzAddrs &regionAddrs(KzRegion region)
    {
        switch (region)
        {
        case KzRegion::Pal:
            return kKzAddrsPal;
        case KzRegion::Eu:
            return kKzAddrsEu;
        case KzRegion::Us:
        default:
            return kKzAddrsUs;
        }
    }
}

KzRegion kzRegion() { return g_region; }

const char *kzRegionTitle(KzRegion r) { return r == KzRegion::Us ? "Killzone (USA)" : "Killzone (Europe)"; }
const char *kzRegionSerial(KzRegion r)
{
    switch (r)
    {
    case KzRegion::Pal:
        return "SCES-52893";
    case KzRegion::Eu:
        return "SCES-52004";
    case KzRegion::Us:
    default:
        return "SCUS-97402";
    }
}
const char *kzRegionBootElf(KzRegion r)
{
    switch (r)
    {
    case KzRegion::Pal:
        return "SCES_528.93";
    case KzRegion::Eu:
        return "SCES_520.04";
    case KzRegion::Us:
    default:
        return "SCUS_974.02";
    }
}
const char *kzRegionBootFolder(KzRegion r)
{
    switch (r)
    {
    case KzRegion::Pal:
        return "disc_pal";
    case KzRegion::Eu:
        return "disc_eu";
    case KzRegion::Us:
    default:
        return "disc";
    }
}
uint32_t kzRegionBootElfXorCrc(KzRegion r)
{
    switch (r)
    {
    case KzRegion::Pal:
        return 0x69FE9BB6u;
    case KzRegion::Eu:
        return 0x6624A78Cu;
    case KzRegion::Us:
    default:
        return 0xCAAEC49Cu;
    }
}

bool kzRegionSupported(KzRegion r)
{
    if (r == KzRegion::Us)
        return true;
#if defined(KZ_HAVE_PAL)
    if (r == KzRegion::Pal)
        return true;
#endif
#if defined(KZ_HAVE_EU)
    if (r == KzRegion::Eu)
        return true;
#endif
    return false;
}

bool kzSelectRegion(KzRegion r, std::string *error)
{
    auto fail = [error](const std::string &msg) {
        if (error)
            *error = msg;
        std::cerr << "[kz] " << msg << std::endl;
        return false;
    };
    if (g_selected)
        return r == g_region ? true : fail("the game region is already chosen; it cannot change while running");
    if (!kzRegionSupported(r))
        return fail(std::string(kzRegionTitle(r)) + " (" + kzRegionSerial(r) +
                    ") is not supported by this build (its generated code is not linked).");
    const KzAddrs &addrs = regionAddrs(r);
    if (!tableComplete(addrs))
        return fail(std::string("internal error: the guest address table for ") + kzRegionSerial(r) + " is incomplete");
    if (r != KzRegion::Us)
    {
        TableRegisterFunction registerTable = nullptr;
        uint32_t versionEnd = 0;
#if defined(KZ_HAVE_PAL)
        if (r == KzRegion::Pal)
        {
            registerTable = &kzPalRegisterFunctionTable;
            versionEnd = kzPalRegisterFunctionTableEnd;
        }
#endif
#if defined(KZ_HAVE_EU)
        if (r == KzRegion::Eu)
        {
            registerTable = &kzEuRegisterFunctionTable;
            versionEnd = kzEuRegisterFunctionTableEnd;
        }
#endif
        if (!registerTable)
            return fail(std::string("internal error: no generated function-table installer for ") + kzRegionSerial(r));

        const uint32_t base = g_ps2RecompiledFunctionTableBase;
        const uint32_t slots = g_ps2RecompiledFunctionTableSlotCount;
        if (versionEnd < base || ((versionEnd - base) >> 2) > slots || versionEnd > g_ps2RecompiledFunctionTableEnd)
        {
            std::ostringstream m;
            m << "internal error: the function table (0x" << std::hex << base << "..0x" << g_ps2RecompiledFunctionTableEnd
              << ", " << std::dec << slots << " slots) does not cover " << kzRegionSerial(r) << " (end 0x" << std::hex
              << versionEnd
              << "); the US code must be generated with a table that covers it";
            return fail(m.str());
        }
        registerTable(g_ps2RecompiledFunctionTable, base, slots);
        std::cout << "[kz] function table: " << kzRegionSerial(r) << " code installed ("
                  << ((versionEnd - base) >> 2) << " of " << slots << " slots)" << std::endl;
    }
    g_region = r;
    kz_detail::g_addrs = &addrs;
    g_selected = true;
    std::cout << "[kz] game: " << kzRegionTitle(r) << " - " << kzRegionSerial(r) << " (" << kzRegionBootElf(r) << ")" << std::endl;
    return true;
}
