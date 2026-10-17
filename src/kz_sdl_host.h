#pragma once

#include "kz_host/core.h"

struct SDL_Window;

[[nodiscard]] kz::host::NativeWindow kzNativeWindow(SDL_Window *window);
