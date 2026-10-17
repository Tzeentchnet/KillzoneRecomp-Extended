#pragma once

#include "kz_host/core.h"

namespace kz::host::detail
{
    [[nodiscard]] void *allocateMemory(
        std::size_t size,
        MemoryProtection protection,
        void *address,
        MemoryPlacement placement);
    void releaseMemory(void *data, std::size_t size) noexcept;
    void protectMemory(void *data, std::size_t size, MemoryProtection protection);
    void flushInstructionCache(void *data, std::size_t size);
}
