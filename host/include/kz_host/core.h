#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kz::host
{
    enum class ErrorCode
    {
        InvalidArgument,
        Unsupported,
        SystemFailure,
    };

    class Error final : public std::runtime_error
    {
    public:
        Error(ErrorCode code, std::uint64_t nativeCode, const std::string &message);

        [[nodiscard]] ErrorCode code() const noexcept;
        [[nodiscard]] std::uint64_t nativeCode() const noexcept;

    private:
        ErrorCode m_code;
        std::uint64_t m_nativeCode;
    };

    enum class MemoryProtection
    {
        NoAccess,
        ReadOnly,
        ReadWrite,
        ReadExecute,
        ReadWriteExecute,
    };

    enum class MemoryPlacement
    {
        Anywhere,
        ExactAddress,
    };

    class MemoryMapping final
    {
    public:
        MemoryMapping() noexcept = default;
        ~MemoryMapping();
        MemoryMapping(MemoryMapping &&other) noexcept;
        MemoryMapping &operator=(MemoryMapping &&other) noexcept;
        MemoryMapping(const MemoryMapping &) = delete;
        MemoryMapping &operator=(const MemoryMapping &) = delete;

        [[nodiscard]] static MemoryMapping allocate(
            std::size_t size,
            MemoryProtection protection,
            void *address = nullptr,
            MemoryPlacement placement = MemoryPlacement::Anywhere);

        [[nodiscard]] void *data() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

        void protect(MemoryProtection protection);
        void flushInstructionCache(std::size_t offset = 0, std::size_t size = 0) const;
        void reset() noexcept;

    private:
        MemoryMapping(void *data, std::size_t size) noexcept;

        void *m_data = nullptr;
        std::size_t m_size = 0;
    };

    [[nodiscard]] std::size_t memoryPageSize();
    [[nodiscard]] std::size_t memoryAllocationGranularity();

    [[nodiscard]] std::filesystem::path executablePath();
    [[nodiscard]] std::filesystem::path executableDirectory();
    [[nodiscard]] std::string pathToUtf8(const std::filesystem::path &path);

    enum class StandardStreamRedirection
    {
        Redirected,
        AlreadyRedirected,
    };

    [[nodiscard]] StandardStreamRedirection redirectStandardStreams(
        const std::filesystem::path &currentLog,
        const std::filesystem::path &previousLog);
    void setEnvironment(std::string_view name, std::string_view value);

    using MonotonicClock = std::chrono::steady_clock;
    [[nodiscard]] MonotonicClock::time_point monotonicNow() noexcept;
    void sleepFor(std::chrono::nanoseconds duration);

    class ManualResetEvent final
    {
    public:
        explicit ManualResetEvent(bool signaled = false);
        ~ManualResetEvent();
        ManualResetEvent(ManualResetEvent &&) noexcept;
        ManualResetEvent &operator=(ManualResetEvent &&) noexcept;
        ManualResetEvent(const ManualResetEvent &) = delete;
        ManualResetEvent &operator=(const ManualResetEvent &) = delete;

        void signal();
        void reset();
        void wait();
        [[nodiscard]] bool waitFor(std::chrono::nanoseconds timeout);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    enum class CpuArchitecture
    {
        Unknown,
        X86_64,
        Arm64,
    };

    enum class SimdLevel
    {
        None,
        Sse2,
        Sse41,
        Avx,
        Avx2,
        Neon,
    };

    struct CpuFeatures
    {
        CpuArchitecture architecture = CpuArchitecture::Unknown;
        SimdLevel simd = SimdLevel::None;
        bool fma = false;
        bool bmi2 = false;
    };

    [[nodiscard]] CpuFeatures cpuFeatures();
    void configureGuestFloatingPoint();
    void setCurrentThreadName(std::string_view name);

    enum class NativeWindowType
    {
        None,
        Win32,
        Cocoa,
        X11,
        Wayland,
    };

    struct NativeWindow
    {
        NativeWindowType type = NativeWindowType::None;
        void *window = nullptr;
        void *display = nullptr;

        [[nodiscard]] static NativeWindow win32(void *window);
        [[nodiscard]] static NativeWindow cocoa(void *window);
        [[nodiscard]] static NativeWindow x11(void *display, std::uintptr_t window);
        [[nodiscard]] static NativeWindow wayland(void *display, void *surface);
        [[nodiscard]] explicit operator bool() const noexcept;
    };

    struct WindowMetrics
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float scale = 1.0f;
    };

    [[nodiscard]] WindowMetrics queryWindowMetrics(const NativeWindow &window);

    void installCrashReporterIfRequested();
    void startStackSampler(int intervalSeconds, int maxFrames, const std::filesystem::path &path);
    void startProfiler(double startSeconds, double durationSeconds, const std::filesystem::path &path);
}
