#include "kz_sdl_host.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <exception>

namespace
{
    int fail(const char *message)
    {
        std::fprintf(stderr, "[kz-host-window-smoke] FAIL: %s\n", message);
        return 1;
    }

    struct SdlVideoSession
    {
        ~SdlVideoSession()
        {
            if (window)
                SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }

        SDL_Window *window = nullptr;
    };
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return fail(SDL_GetError());

    SdlVideoSession session;
    session.window = SDL_CreateWindow(
        "Killzone host window smoke",
        320,
        180,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!session.window)
        return fail(SDL_GetError());

    try
    {
        const kz::host::NativeWindow nativeWindow = kzNativeWindow(session.window);
        if (!nativeWindow)
            return fail("SDL did not expose a native window");
#if defined(_WIN32)
        if (nativeWindow.type != kz::host::NativeWindowType::Win32)
            return fail("SDL exposed the wrong native-window type");
#elif defined(__APPLE__)
        if (nativeWindow.type != kz::host::NativeWindowType::Cocoa)
            return fail("SDL exposed the wrong native-window type");
#endif

        const kz::host::WindowMetrics metrics = kz::host::queryWindowMetrics(nativeWindow);
        if (metrics.width == 0 || metrics.height == 0 || !std::isfinite(metrics.scale) || metrics.scale <= 0.0f)
            return fail("native window metrics are invalid");

        int pixelWidth = 0;
        int pixelHeight = 0;
        if (!SDL_GetWindowSizeInPixels(session.window, &pixelWidth, &pixelHeight))
            return fail(SDL_GetError());
        if (std::abs(static_cast<int>(metrics.width) - pixelWidth) > 1
            || std::abs(static_cast<int>(metrics.height) - pixelHeight) > 1)
        {
            return fail("native and SDL drawable sizes disagree");
        }

        const Uint32 eventType = SDL_RegisterEvents(1);
        if (eventType == static_cast<Uint32>(-1))
            return fail(SDL_GetError());
        SDL_Event pushed{};
        pushed.type = eventType;
        pushed.user.code = 0x4b5a;
        if (!SDL_PushEvent(&pushed))
            return fail(SDL_GetError());

        bool received = false;
        const Uint64 deadline = SDL_GetTicks() + 1000;
        while (!received && SDL_GetTicks() < deadline)
        {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                if (event.type == eventType && event.user.code == pushed.user.code)
                    received = true;
            }
            if (!received)
                SDL_Delay(1);
        }
        if (!received)
            return fail("the SDL event loop did not receive the pushed event");

        std::printf(
            "[kz-host-window-smoke] passed (driver=%s pixels=%ux%u scale=%.2f)\n",
            SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?",
            metrics.width,
            metrics.height,
            metrics.scale);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[kz-host-window-smoke] FAIL: %s\n", error.what());
        return 1;
    }
}
