// Optional skip of the boot intro movie (GitHub issue #5).
//
// FUN_0026ECC8 (PAL 0x26F338, kzAddrs().playMovie) is the game's blocking movie player (a0 = movie name string object, *a0 + 0x10 = the characters).
// It first checks that the file exists and returns 0 without playing when it does not; callers then carry on as
// after the movie. With the option on, the hook takes that path for the intro movie only ("Movies/Intro/Intro").
// Story cutscenes, the attract loop and the menu background movies are untouched.
// KZ_MOVIE_LOG=1 logs every movie the player is asked to play.
//
// Optional skip of the boot logo screens (issue #24). They are a front-end script page, "Logos" (Lua, FILES03.DAT): its
// LogosUpdate fades the SCEA/SCEE logo, the legal screen and the Guerrilla logo on a timeline and at 21 s calls
// SwitchToMenu("MemoryCardTest"), which leads on to the intro movie and the main menu. FUN_001A8958 (PAL 0x1A8B30,
// kzAddrs().menuPageUpdate) runs the current page's Update each frame, with the script clock nowSeconds = menu+0x170
// (milliseconds) x 0.001. While the page is "Logos", the hook moves that counter forward by kLogoStepMs per frame, so the
// script plays its own timeline to the end in a few frames. The page is still created (LogosInit, and the KILLZONE logo
// resource loaded for the main menu), the memory card check and everything after it run unchanged.
// KZ_SKIP_LOGOS=0/1 overrides the setting; KZ_LOGO_LOG=1 logs the counter on each Logos frame.

#include "kz_movies.h"

#include "kz_config.h"
#include "kz_addrs.h"
#include "kzgs_temporal.h"
#include "ps2_runtime.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
    constexpr const char *kIntroMovie = "Movies/Intro/Intro";
    constexpr std::size_t kMaxMovieNameBytes = 128;

    PS2Runtime::RecompiledFunction g_origPlayMovie = nullptr;
    bool g_skipIntro = false;
    bool g_skipLogos = false;
    bool g_logoLog = false;
    constexpr uint32_t kLogoStepMs = 2000; // 21 s timeline in about 11 frames
    PS2Runtime::RecompiledFunction g_origMenuPageUpdate = nullptr;
    bool g_log = false;
    bool g_temporalDiagnostics = false;
    std::atomic<uint64_t> g_temporalEvents{0};
    std::string g_lastMenuPage;

    bool guestOffset(uint32_t address, std::size_t size, uint32_t &offset)
    {
        uint32_t physical = address;
        if (address >= PS2_RAM_SIZE)
        {
            if ((address & 0xC0000000u) != 0x80000000u)
                return false;
            physical = address & 0x1FFFFFFFu;
        }
        if (physical > PS2_RAM_SIZE || size > PS2_RAM_SIZE - physical)
            return false;
        offset = physical;
        return true;
    }

    bool readGuestU32(const uint8_t *rdram, uint32_t address, uint32_t &value)
    {
        uint32_t offset = 0;
        if (!guestOffset(address, sizeof(value), offset))
            return false;
        std::memcpy(&value, rdram + offset, sizeof(value));
        return true;
    }

    bool writeGuestU32(uint8_t *rdram, uint32_t address, uint32_t value)
    {
        uint32_t offset = 0;
        if (!guestOffset(address, sizeof(value), offset))
            return false;
        std::memcpy(rdram + offset, &value, sizeof(value));
        return true;
    }

    bool guestString(const uint8_t *rdram, uint32_t stringObject, std::string &name)
    {
        uint32_t buffer = 0;
        if (!readGuestU32(rdram, stringObject, buffer) ||
            buffer > std::numeric_limits<uint32_t>::max() - 0x10u)
        {
            return false;
        }
        uint32_t offset = 0;
        if (!guestOffset(buffer + 0x10u, 1, offset))
            return false;
        const std::size_t limit = std::min<std::size_t>(kMaxMovieNameBytes, PS2_RAM_SIZE - offset);
        std::size_t length = 0;
        while (length < limit && rdram[offset + length] != 0)
            ++length;
        if (length == limit)
            return false;
        name.assign(reinterpret_cast<const char *>(rdram + offset), length);
        return true;
    }

    void playMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kzAddrs().playMovie)
        {
            if (g_temporalDiagnostics)
                g_temporalEvents.fetch_or(KZGS_TEMPORAL_RESET_MOVIE, std::memory_order_relaxed);
            std::string name;
            const bool validName = (g_skipIntro || g_log) && guestString(rdram, getRegU32(ctx, 4), name);
            const bool skip = validName && g_skipIntro && name == kIntroMovie;
            if (g_log || skip)
                std::printf(
                    "[kz] movie \"%s\"%s\n",
                    validName ? name.c_str() : "<invalid guest string>",
                    skip ? ": skipped (Skip intro movie)" : "");
            if (skip)
            {
                ctx->pc = getRegU32(ctx, 31);
                setReturnS32(ctx, 0);
                return;
            }
        }
        g_origPlayMovie(rdram, ctx, rt);
    }

    void menuPageUpdate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t menu = getRegU32(ctx, 4);
        std::string page;
        uint32_t ms = 0;
        const bool entry = ctx->pc == kzAddrs().menuPageUpdate;
        const bool validPage = entry && menu <= std::numeric_limits<uint32_t>::max() - 0x170u &&
            guestString(rdram, menu + 8u, page);
        if (g_temporalDiagnostics && entry && (!validPage || page != g_lastMenuPage))
        {
            g_temporalEvents.fetch_or(KZGS_TEMPORAL_RESET_MENU, std::memory_order_relaxed);
            g_lastMenuPage = validPage ? page : std::string{};
        }
        if (g_skipLogos && validPage && page == "Logos" &&
            readGuestU32(rdram, menu + 0x170u, ms))
        {
            static int frames = 0;
            ms += kLogoStepMs;
            writeGuestU32(rdram, menu + 0x170u, ms);
            if (frames++ == 0)
                std::printf("[kz] boot logos: skipping (Skip logos)\n");
            if (g_logoLog)
                std::printf("[kz] logos frame %d: menu+0x170 = %u ms\n", frames, ms);
        }
        g_origMenuPageUpdate(rdram, ctx, rt);
    }
}

uint64_t kzMoviesTakeTemporalEvents()
{
    return g_temporalEvents.exchange(0, std::memory_order_relaxed);
}

int kzMoviesSelfTest()
{
    int failures = 0;
    const auto check = [&](bool condition, const char *label) {
        if (!condition)
        {
            std::fprintf(stderr, "[selftest] FAIL: %s\n", label);
            ++failures;
        }
    };
    const auto savedMenu = g_origMenuPageUpdate, savedMovie = g_origPlayMovie;
    const bool savedSkipLogos = g_skipLogos, savedSkipIntro = g_skipIntro;
    const bool savedLog = g_log, savedLogoLog = g_logoLog, savedDiagnostics = g_temporalDiagnostics;
    const uint64_t savedEvents = g_temporalEvents.exchange(0, std::memory_order_relaxed);
    const std::string savedPage = g_lastMenuPage;
    g_origMenuPageUpdate = g_origPlayMovie = +[](uint8_t *, R5900Context *ctx, PS2Runtime *) { ctx->pc = 0; };
    g_skipLogos = g_skipIntro = g_log = g_logoLog = false;
    g_temporalDiagnostics = true;
    g_lastMenuPage.clear();

    constexpr uint32_t menu = 0x00100000, buffer = 0x00100200;
    std::vector<uint8_t> ram(0x00100400);
    std::memcpy(ram.data() + menu + 8, &buffer, sizeof(buffer));
    std::memcpy(ram.data() + buffer + 0x10, "Logos", 6);
    uint32_t ms = 1234;
    std::memcpy(ram.data() + menu + 0x170, &ms, sizeof(ms));
    R5900Context ctx;
    ctx.r[4] = _mm_cvtsi32_si128(static_cast<int>(menu));
    ctx.pc = kzAddrs().menuPageUpdate;
    menuPageUpdate(ram.data(), &ctx, nullptr);
    uint32_t after = 0;
    std::memcpy(&after, ram.data() + menu + 0x170, sizeof(after));
    check(after == ms && ctx.pc == 0 && kzMoviesTakeTemporalEvents() == KZGS_TEMPORAL_RESET_MENU,
          "diagnostic-only menu hook forwards without mutating the logo timer");
    ctx.pc = kzAddrs().menuPageUpdate;
    menuPageUpdate(ram.data(), &ctx, nullptr);
    check(kzMoviesTakeTemporalEvents() == 0, "unchanged menu page does not repeat a transition");
    std::memcpy(ram.data() + buffer + 0x10, "Next", 5);
    ctx.pc = kzAddrs().menuPageUpdate;
    menuPageUpdate(ram.data(), &ctx, nullptr);
    check(kzMoviesTakeTemporalEvents() == KZGS_TEMPORAL_RESET_MENU,
          "a changed menu page marks a discontinuity");
    ctx.pc = kzAddrs().playMovie;
    playMovie(ram.data(), &ctx, nullptr);
    check(ctx.pc == 0 && kzMoviesTakeTemporalEvents() == KZGS_TEMPORAL_RESET_MOVIE,
          "diagnostic-only movie entry forwards and marks a discontinuity");

    g_origMenuPageUpdate = savedMenu;
    g_origPlayMovie = savedMovie;
    g_skipLogos = savedSkipLogos;
    g_skipIntro = savedSkipIntro;
    g_log = savedLog;
    g_logoLog = savedLogoLog;
    g_temporalDiagnostics = savedDiagnostics;
    g_lastMenuPage = savedPage;
    g_temporalEvents.store(savedEvents, std::memory_order_relaxed);
    return failures;
}

void kzMoviesInstall(PS2Runtime &runtime, bool temporalDiagnostics)
{
    g_temporalDiagnostics = temporalDiagnostics;
    g_temporalEvents.store(0, std::memory_order_relaxed);
    g_lastMenuPage.clear();
    auto applyBoolOverride = [](const char *name, bool &target) {
        if (const char *value = std::getenv(name); value && *value)
        {
            if (std::strcmp(value, "0") == 0)
                target = false;
            else if (std::strcmp(value, "1") == 0)
                target = true;
            else
                std::fprintf(stderr, "[kz] ignoring invalid %s=%s (expected 0 or 1)\n", name, value);
        }
    };

    g_skipIntro = kzConfig().skipLongIntro;
    applyBoolOverride("KZ_SKIP_LONG_INTRO", g_skipIntro);
    const char *movieLog = std::getenv("KZ_MOVIE_LOG");
    g_log = movieLog && std::strcmp(movieLog, "0") != 0;
    g_skipLogos = kzConfig().skipLogos;
    applyBoolOverride("KZ_SKIP_LOGOS", g_skipLogos);
    const char *logoLog = std::getenv("KZ_LOGO_LOG");
    g_logoLog = logoLog && std::strcmp(logoLog, "0") != 0;
    if (g_skipLogos || g_temporalDiagnostics)
    {
        const uint32_t kUpdate = kzAddrs().menuPageUpdate;
        g_origMenuPageUpdate = runtime.hasFunction(kUpdate) ? runtime.lookupFunction(kUpdate) : nullptr;
        if (!g_origMenuPageUpdate || !runtime.replaceFunction(kUpdate, &menuPageUpdate))
            std::printf("[kz] logo skip hook FAILED\n");
    }
    if (!g_skipIntro && !g_log && !g_temporalDiagnostics)
        return;
    const uint32_t kPlayMovie = kzAddrs().playMovie;
    g_origPlayMovie = runtime.hasFunction(kPlayMovie) ? runtime.lookupFunction(kPlayMovie) : nullptr;
    if (!g_origPlayMovie || !runtime.replaceFunction(kPlayMovie, &playMovie))
        std::printf("[kz] movie hook FAILED\n");
}
