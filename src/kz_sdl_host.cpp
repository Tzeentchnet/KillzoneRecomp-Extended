#include "kz_sdl_host.h"

#include <SDL3/SDL.h>

kz::host::NativeWindow kzNativeWindow(SDL_Window *window)
{
    if (!window)
        return {};

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
#if defined(_WIN32)
    void *handle = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!handle)
        throw kz::host::Error(kz::host::ErrorCode::SystemFailure, 0, "SDL did not expose a Win32 HWND");
    return kz::host::NativeWindow::win32(handle);
#elif defined(__APPLE__)
    void *handle = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!handle)
        throw kz::host::Error(kz::host::ErrorCode::SystemFailure, 0, "SDL did not expose a Cocoa NSWindow");
    return kz::host::NativeWindow::cocoa(handle);
#elif defined(__linux__)
    if (void *surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr))
    {
        void *display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        return kz::host::NativeWindow::wayland(display, surface);
    }
    void *display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    const auto handle = static_cast<std::uintptr_t>(
        SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    return kz::host::NativeWindow::x11(display, handle);
#else
    throw kz::host::Error(kz::host::ErrorCode::Unsupported, 0, "SDL native-window extraction is not implemented for this host");
#endif
}
