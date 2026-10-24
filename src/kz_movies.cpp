#include "kz_movies.h"

#include "kz_config.h"
#include "kz_hook_addresses.h"

#include "ps2_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace
{
    constexpr const char *kIntroMovie = "Movies/Intro/Intro";
    constexpr std::size_t kMaxMovieNameBytes = 128;

    PS2Runtime::RecompiledFunction g_origPlayMovie = nullptr;
    bool g_skipIntro = false;
    bool g_logMovies = false;

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

    bool movieName(const uint8_t *rdram, uint32_t stringObject, std::string &name)
    {
        uint32_t buffer = 0;
        if (!readGuestU32(rdram, stringObject, buffer)
            || buffer > std::numeric_limits<uint32_t>::max() - 0x10u)
        {
            return false;
        }
        uint32_t offset = 0;
        if (!guestOffset(buffer + 0x10u, 1, offset))
            return false;
        const std::size_t limit =
            std::min<std::size_t>(kMaxMovieNameBytes, PS2_RAM_SIZE - offset);
        std::size_t length = 0;
        while (length < limit && rdram[offset + length] != 0)
            ++length;
        if (length == limit)
            return false;
        name.assign(reinterpret_cast<const char *>(rdram + offset), length);
        return true;
    }

    void playMovie(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kz_hook::MoviePlay)
        {
            std::string name;
            const bool validName = movieName(rdram, getRegU32(ctx, 4), name);
            const bool skip = validName && g_skipIntro && name == kIntroMovie;
            if (g_logMovies || skip)
            {
                std::printf(
                    "[kz] movie \"%s\"%s\n",
                    validName ? name.c_str() : "<invalid guest string>",
                    skip ? ": skipped (Skip intro movie)" : "");
            }
            if (skip)
            {
                setReturnS32(ctx, 0);
                ctx->pc = getRegU32(ctx, 31);
                return;
            }
        }
        g_origPlayMovie(rdram, ctx, runtime);
    }
}

void kzMoviesInstall(PS2Runtime &runtime)
{
    g_skipIntro = kzConfig().skipLongIntro;
    if (const char *value = std::getenv("KZ_SKIP_LONG_INTRO"); value && *value)
    {
        if (std::strcmp(value, "0") == 0)
            g_skipIntro = false;
        else if (std::strcmp(value, "1") == 0)
            g_skipIntro = true;
        else
            std::fprintf(stderr, "[kz] ignoring invalid KZ_SKIP_LONG_INTRO=%s (expected 0 or 1)\n", value);
    }
    const char *movieLog = std::getenv("KZ_MOVIE_LOG");
    g_logMovies = movieLog && std::strcmp(movieLog, "0") != 0;
    if (!g_skipIntro && !g_logMovies)
        return;

    g_origPlayMovie = runtime.hasFunction(kz_hook::MoviePlay)
                          ? runtime.lookupFunction(kz_hook::MoviePlay)
                          : nullptr;
    if (!g_origPlayMovie || !runtime.replaceFunction(kz_hook::MoviePlay, &playMovie))
        std::fprintf(stderr, "[kz] movie hook: FAILED\n");
}
