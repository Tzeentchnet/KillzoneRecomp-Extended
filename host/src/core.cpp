#include "kz_host/core.h"

#include "internal.h"

#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace kz::host
{
    Error::Error(ErrorCode code, std::uint64_t nativeCode, const std::string &message)
        : std::runtime_error(message), m_code(code), m_nativeCode(nativeCode)
    {
    }

    ErrorCode Error::code() const noexcept
    {
        return m_code;
    }

    std::uint64_t Error::nativeCode() const noexcept
    {
        return m_nativeCode;
    }

    MemoryMapping::MemoryMapping(void *data, std::size_t size) noexcept
        : m_data(data), m_size(size)
    {
    }

    MemoryMapping::~MemoryMapping()
    {
        reset();
    }

    MemoryMapping::MemoryMapping(MemoryMapping &&other) noexcept
        : m_data(std::exchange(other.m_data, nullptr)),
          m_size(std::exchange(other.m_size, 0))
    {
    }

    MemoryMapping &MemoryMapping::operator=(MemoryMapping &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_data = std::exchange(other.m_data, nullptr);
            m_size = std::exchange(other.m_size, 0);
        }
        return *this;
    }

    MemoryMapping MemoryMapping::allocate(
        std::size_t size,
        MemoryProtection protection,
        void *address,
        MemoryPlacement placement)
    {
        if (size == 0)
            throw Error(ErrorCode::InvalidArgument, 0, "Cannot allocate a zero-byte host memory mapping");
        if (placement == MemoryPlacement::ExactAddress && address == nullptr)
            throw Error(ErrorCode::InvalidArgument, 0, "An exact host memory mapping requires a non-null address");
        return MemoryMapping(detail::allocateMemory(size, protection, address, placement), size);
    }

    void *MemoryMapping::data() const noexcept
    {
        return m_data;
    }

    std::size_t MemoryMapping::size() const noexcept
    {
        return m_size;
    }

    MemoryMapping::operator bool() const noexcept
    {
        return m_data != nullptr;
    }

    void MemoryMapping::protect(MemoryProtection protection)
    {
        if (!m_data)
            throw Error(ErrorCode::InvalidArgument, 0, "Cannot protect an empty host memory mapping");
        detail::protectMemory(m_data, m_size, protection);
    }

    void MemoryMapping::flushInstructionCache(std::size_t offset, std::size_t size) const
    {
        if (!m_data)
            throw Error(ErrorCode::InvalidArgument, 0, "Cannot flush an empty host memory mapping");
        if (offset > m_size)
            throw Error(ErrorCode::InvalidArgument, 0, "Instruction-cache flush offset exceeds the host memory mapping");
        const std::size_t remaining = m_size - offset;
        const std::size_t flushSize = size == 0 ? remaining : size;
        if (flushSize > remaining)
            throw Error(ErrorCode::InvalidArgument, 0, "Instruction-cache flush range exceeds the host memory mapping");
        detail::flushInstructionCache(static_cast<std::byte *>(m_data) + offset, flushSize);
    }

    void MemoryMapping::reset() noexcept
    {
        if (m_data)
            detail::releaseMemory(m_data, m_size);
        m_data = nullptr;
        m_size = 0;
    }

    MonotonicClock::time_point monotonicNow() noexcept
    {
        return MonotonicClock::now();
    }

    void sleepFor(std::chrono::nanoseconds duration)
    {
        if (duration < std::chrono::nanoseconds::zero())
            throw Error(ErrorCode::InvalidArgument, 0, "Host sleep duration cannot be negative");
        std::this_thread::sleep_for(duration);
    }

    std::string pathToUtf8(const std::filesystem::path &path)
    {
        const std::u8string text = path.generic_u8string();
        return {reinterpret_cast<const char *>(text.data()), text.size()};
    }

    struct ManualResetEvent::Impl
    {
        explicit Impl(bool initial)
            : signaled(initial)
        {
        }

        std::mutex mutex;
        std::condition_variable condition;
        bool signaled;
    };

    ManualResetEvent::ManualResetEvent(bool signaled)
        : m_impl(std::make_unique<Impl>(signaled))
    {
    }

    ManualResetEvent::~ManualResetEvent() = default;
    ManualResetEvent::ManualResetEvent(ManualResetEvent &&) noexcept = default;
    ManualResetEvent &ManualResetEvent::operator=(ManualResetEvent &&) noexcept = default;

    void ManualResetEvent::signal()
    {
        {
            std::lock_guard lock(m_impl->mutex);
            m_impl->signaled = true;
        }
        m_impl->condition.notify_all();
    }

    void ManualResetEvent::reset()
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->signaled = false;
    }

    void ManualResetEvent::wait()
    {
        std::unique_lock lock(m_impl->mutex);
        m_impl->condition.wait(lock, [this] { return m_impl->signaled; });
    }

    bool ManualResetEvent::waitFor(std::chrono::nanoseconds timeout)
    {
        if (timeout < std::chrono::nanoseconds::zero())
            throw Error(ErrorCode::InvalidArgument, 0, "Host wait timeout cannot be negative");
        std::unique_lock lock(m_impl->mutex);
        return m_impl->condition.wait_for(lock, timeout, [this] { return m_impl->signaled; });
    }

    NativeWindow NativeWindow::win32(void *window)
    {
        if (!window)
            throw Error(ErrorCode::InvalidArgument, 0, "A Win32 native window requires a non-null HWND");
        return {NativeWindowType::Win32, window, nullptr};
    }

    NativeWindow NativeWindow::cocoa(void *window)
    {
        if (!window)
            throw Error(ErrorCode::InvalidArgument, 0, "A Cocoa native window requires a non-null NSWindow");
        return {NativeWindowType::Cocoa, window, nullptr};
    }

    NativeWindow NativeWindow::x11(void *display, std::uintptr_t window)
    {
        if (!display || window == 0)
            throw Error(ErrorCode::InvalidArgument, 0, "An X11 native window requires a Display and Window");
        return {NativeWindowType::X11, reinterpret_cast<void *>(window), display};
    }

    NativeWindow NativeWindow::wayland(void *display, void *surface)
    {
        if (!display || !surface)
            throw Error(ErrorCode::InvalidArgument, 0, "A Wayland native window requires a display and surface");
        return {NativeWindowType::Wayland, surface, display};
    }

    NativeWindow::operator bool() const noexcept
    {
        return type != NativeWindowType::None && window != nullptr;
    }
}
