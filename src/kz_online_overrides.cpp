// Killzone Online (LOADER.ELF) runtime overrides.

#include "game_overrides.h"
#include "kz_sif_modules.h"
#include "ps2_runtime.h"

#include <cstdint>
#include <iostream>

namespace
{
    constexpr uint32_t kEntry = 0x00100BE8u;
    constexpr uint32_t kSifStopModule = 0x00143B30u;
    constexpr uint32_t kSifUnloadModule = 0x00143D38u;
    constexpr uint32_t kSifSearchModuleByName = 0x00143DC8u;
    constexpr uint32_t kOnlineSceCdInit = 0x00541CE8u;
    constexpr uint32_t kOnlineSceCdDiskReady = 0x005421E8u;
    constexpr uint32_t kOnlineSceCdGetDiskType = 0x005424A0u;
    constexpr uint32_t kOnlineSceCdMmode = 0x005424C8u;
    constexpr uint32_t kOnlineSceCdReadClock = 0x005425A0u;

    void applyKillzoneOnline(PS2Runtime &runtime)
    {
        if (!ps2_game_overrides::bindAddressHandler(runtime, kSifStopModule, "SifStopModule"))
            std::cerr << "[kz] failed to bind online sceSifStopModule" << std::endl;
        if (!runtime.replaceFunction(kSifUnloadModule, &kzSceSifUnloadModule))
            std::cerr << "[kz] failed to bind online sceSifUnloadModule" << std::endl;
        if (!runtime.replaceFunction(kSifSearchModuleByName, &kzSceSifSearchModuleByName))
            std::cerr << "[kz] failed to bind online sceSifSearchModuleByName" << std::endl;
        if (!ps2_game_overrides::bindAddressHandler(runtime, kOnlineSceCdInit, "sceCdInit"))
            std::cerr << "[kz] failed to bind online sceCdInit" << std::endl;
        if (!ps2_game_overrides::bindAddressHandler(runtime, kOnlineSceCdDiskReady, "sceCdDiskReady"))
            std::cerr << "[kz] failed to bind online sceCdDiskReady" << std::endl;
        if (!ps2_game_overrides::bindAddressHandler(runtime, kOnlineSceCdGetDiskType, "sceCdGetDiskType"))
            std::cerr << "[kz] failed to bind online sceCdGetDiskType" << std::endl;
        if (!ps2_game_overrides::bindAddressHandler(runtime, kOnlineSceCdMmode, "sceCdMmode"))
            std::cerr << "[kz] failed to bind online sceCdMmode" << std::endl;
        if (!ps2_game_overrides::bindAddressHandler(runtime, kOnlineSceCdReadClock, "sceCdReadClock"))
            std::cerr << "[kz] failed to bind online sceCdReadClock" << std::endl;
    }
}

// Static online bundles retain the retail filename and entry point but append pre-relocated REL segments.
PS2_REGISTER_GAME_OVERRIDE("Killzone Online", "LOADER.ELF", kEntry, 0u, applyKillzoneOnline)
