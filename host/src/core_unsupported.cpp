#include "kz_host/core.h"

#include "internal.h"

#include <string>

namespace
{
    [[noreturn]] void unsupported(const char *operation)
    {
        throw kz::host::Error(
            kz::host::ErrorCode::Unsupported,
            0,
            std::string(operation) + " is not implemented for this host platform");
    }
}

namespace kz::host::detail
{
    void *allocateMemory(std::size_t, MemoryProtection, void *, MemoryPlacement)
    {
        unsupported("Host memory mapping");
    }

    void releaseMemory(void *, std::size_t) noexcept
    {
    }

    void protectMemory(void *, std::size_t, MemoryProtection)
    {
        unsupported("Host memory protection");
    }

    void flushInstructionCache(void *, std::size_t)
    {
        unsupported("Host instruction-cache invalidation");
    }
}

namespace kz::host
{
    std::size_t memoryPageSize()
    {
        unsupported("Host memory page-size discovery");
    }

    std::size_t memoryAllocationGranularity()
    {
        unsupported("Host memory allocation-granularity discovery");
    }

    std::filesystem::path executablePath()
    {
        unsupported("Executable-path discovery");
    }

    std::filesystem::path executableDirectory()
    {
        unsupported("Executable-directory discovery");
    }

    StandardStreamRedirection redirectStandardStreams(const std::filesystem::path &, const std::filesystem::path &)
    {
        unsupported("Standard-stream redirection");
    }

    void setEnvironment(std::string_view, std::string_view)
    {
        unsupported("Environment-variable mutation");
    }

    CpuFeatures cpuFeatures()
    {
#if defined(__aarch64__)
        return {CpuArchitecture::Arm64, SimdLevel::Neon, false, false};
#elif defined(__x86_64__)
        return {CpuArchitecture::X86_64, SimdLevel::Sse2, false, false};
#else
        return {};
#endif
    }

    void configureGuestFloatingPoint()
    {
        unsupported("PS2 guest floating-point setup");
    }

    void setCurrentThreadName(std::string_view)
    {
        unsupported("Host thread naming");
    }

    WindowMetrics queryWindowMetrics(const NativeWindow &)
    {
        unsupported("Native-window metrics");
    }

    void installCrashReporterIfRequested()
    {
        unsupported("Crash reporting");
    }

    void startStackSampler(int, int, const std::filesystem::path &)
    {
        unsupported("Stack sampling");
    }

    void startProfiler(double, double, const std::filesystem::path &)
    {
        unsupported("Statistical profiling");
    }
}
