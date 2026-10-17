#include "kz_host/core.h"

#include "internal.h"

#import <Cocoa/Cocoa.h>

#include <libkern/OSCacheControl.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <ios>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    [[noreturn]] void throwPosix(const std::string &operation, int error = errno)
    {
        throw kz::host::Error(
            kz::host::ErrorCode::SystemFailure,
            static_cast<std::uint64_t>(error),
            operation + " failed with POSIX error " + std::to_string(error));
    }

    [[nodiscard]] int nativeProtection(kz::host::MemoryProtection protection)
    {
        using kz::host::MemoryProtection;
        switch (protection)
        {
        case MemoryProtection::NoAccess:
            return PROT_NONE;
        case MemoryProtection::ReadOnly:
            return PROT_READ;
        case MemoryProtection::ReadWrite:
            return PROT_READ | PROT_WRITE;
        case MemoryProtection::ReadExecute:
            return PROT_READ | PROT_EXEC;
        case MemoryProtection::ReadWriteExecute:
            throw kz::host::Error(
                kz::host::ErrorCode::Unsupported,
                0,
                "Writable and executable host memory is prohibited on macOS");
        }
        throw kz::host::Error(kz::host::ErrorCode::InvalidArgument, 0, "Unknown host memory protection");
    }

    [[noreturn]] void unsupported(const char *operation)
    {
        throw kz::host::Error(
            kz::host::ErrorCode::Unsupported,
            0,
            std::string(operation) + " is not implemented for the macOS host");
    }
}

namespace kz::host::detail
{
    void *allocateMemory(
        std::size_t size,
        MemoryProtection protection,
        void *address,
        MemoryPlacement placement)
    {
        if (placement == MemoryPlacement::ExactAddress
            && reinterpret_cast<std::uintptr_t>(address) % memoryPageSize() != 0)
        {
            throw Error(ErrorCode::InvalidArgument, 0, "An exact host memory address must be page aligned");
        }

        void *result = mmap(address, size, nativeProtection(protection), MAP_PRIVATE | MAP_ANON, -1, 0);
        if (result == MAP_FAILED)
            throwPosix("Allocating host memory");
        if (placement == MemoryPlacement::ExactAddress && result != address)
        {
            munmap(result, size);
            throw Error(
                ErrorCode::SystemFailure,
                0,
                "macOS did not honor an exact host memory address");
        }
        return result;
    }

    void releaseMemory(void *data, std::size_t size) noexcept
    {
        if (munmap(data, size) != 0)
            std::fprintf(stderr, "[kz-host] munmap failed with POSIX error %d\n", errno);
    }

    void protectMemory(void *data, std::size_t size, MemoryProtection protection)
    {
        if (mprotect(data, size, nativeProtection(protection)) != 0)
            throwPosix("Protecting host memory");
    }

    void flushInstructionCache(void *data, std::size_t size)
    {
        if (size != 0)
            sys_icache_invalidate(data, size);
    }
}

namespace kz::host
{
    std::size_t memoryPageSize()
    {
        const long size = sysconf(_SC_PAGESIZE);
        if (size <= 0)
            throwPosix("Discovering the host memory page size");
        return static_cast<std::size_t>(size);
    }

    std::size_t memoryAllocationGranularity()
    {
        return memoryPageSize();
    }

    std::filesystem::path executablePath()
    {
        std::uint32_t size = 0;
        if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0)
            throw Error(ErrorCode::SystemFailure, 0, "macOS did not report the executable-path buffer size");

        std::vector<char> path(size);
        if (_NSGetExecutablePath(path.data(), &size) != 0)
            throw Error(ErrorCode::SystemFailure, 0, "Resolving the executable path failed");

        std::error_code error;
        const std::filesystem::path canonical = std::filesystem::canonical(path.data(), error);
        if (error)
        {
            throw Error(
                ErrorCode::SystemFailure,
                static_cast<std::uint64_t>(error.value()),
                "Canonicalizing the executable path failed: " + error.message());
        }
        return canonical;
    }

    std::filesystem::path executableDirectory()
    {
        const std::filesystem::path path = executablePath();
        if (!path.has_parent_path())
            throw Error(ErrorCode::SystemFailure, 0, "The executable path has no parent directory");
        return path.parent_path();
    }

    StandardStreamRedirection redirectStandardStreams(
        const std::filesystem::path &currentLog,
        const std::filesystem::path &previousLog)
    {
        if (fcntl(STDOUT_FILENO, F_GETFD) != -1)
        {
            if (isatty(STDOUT_FILENO) == 0)
                return StandardStreamRedirection::AlreadyRedirected;
        }
        else if (errno != EBADF)
        {
            throwPosix("Inspecting stdout before host-log redirection");
        }

        std::error_code error;
        const bool currentExists = std::filesystem::exists(currentLog, error);
        if (error)
            throw Error(ErrorCode::SystemFailure, error.value(), "Inspecting the current host log failed: " + error.message());
        if (currentExists)
        {
            std::filesystem::remove(previousLog, error);
            if (error)
                throw Error(ErrorCode::SystemFailure, error.value(), "Removing the previous host log failed: " + error.message());
            std::filesystem::rename(currentLog, previousLog, error);
            if (error)
                throw Error(ErrorCode::SystemFailure, error.value(), "Rotating the current host log failed: " + error.message());
        }

        if (!std::freopen(currentLog.c_str(), "w", stdout))
            throwPosix("Redirecting stdout to the host log");
        if (!std::freopen(currentLog.c_str(), "a", stderr))
            throwPosix("Redirecting stderr to the host log");
        std::setvbuf(stdout, nullptr, _IOLBF, 4096);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        std::ios::sync_with_stdio(true);
        return StandardStreamRedirection::Redirected;
    }

    void setEnvironment(std::string_view name, std::string_view value)
    {
        if (name.empty()
            || name.find('=') != std::string_view::npos
            || name.find('\0') != std::string_view::npos
            || value.find('\0') != std::string_view::npos)
        {
            throw Error(ErrorCode::InvalidArgument, 0, "Host environment variable name or value is invalid");
        }

        const std::string nameString(name);
        const std::string valueString(value);
        if (setenv(nameString.c_str(), valueString.c_str(), 1) != 0)
            throwPosix("Setting host environment variable '" + nameString + "'");
    }

    CpuFeatures cpuFeatures()
    {
#if defined(__aarch64__) || defined(__arm64__)
        return {CpuArchitecture::Arm64, SimdLevel::Neon, true, false};
#elif defined(__x86_64__)
        return {CpuArchitecture::X86_64, SimdLevel::Sse2, false, false};
#else
        return {};
#endif
    }

    void configureGuestFloatingPoint()
    {
#if defined(__aarch64__) || defined(__arm64__)
        std::uint64_t fpcr = 0;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= std::uint64_t{1} << 24;
        __asm__ volatile("msr fpcr, %0\nisb" : : "r"(fpcr) : "memory");
#else
        throw Error(ErrorCode::Unsupported, 0, "PS2 guest floating-point setup requires Apple Silicon");
#endif
    }

    void setCurrentThreadName(std::string_view name)
    {
        if (name.empty() || name.size() > 63 || name.find('\0') != std::string_view::npos)
            throw Error(ErrorCode::InvalidArgument, 0, "macOS thread names must contain 1 to 63 UTF-8 bytes");
        const std::string nameString(name);
        const int result = pthread_setname_np(nameString.c_str());
        if (result != 0)
            throwPosix("Setting the host thread name", result);
    }

    WindowMetrics queryWindowMetrics(const NativeWindow &window)
    {
        if (window.type != NativeWindowType::Cocoa)
            throw Error(ErrorCode::Unsupported, 0, "Window metrics require a Cocoa native window on the macOS host");
        if (!window.window)
            throw Error(ErrorCode::InvalidArgument, 0, "Window metrics require a non-null NSWindow");
        if (![NSThread isMainThread])
            throw Error(ErrorCode::SystemFailure, 0, "Cocoa window metrics must be queried on the main thread");

        @autoreleasepool
        {
            NSWindow *nativeWindow = (__bridge NSWindow *)window.window;
            NSView *contentView = nativeWindow.contentView;
            if (!contentView)
                throw Error(ErrorCode::SystemFailure, 0, "The Cocoa window has no content view");

            const NSRect backingBounds = [contentView convertRectToBacking:contentView.bounds];
            const CGFloat scale = nativeWindow.backingScaleFactor;
            if (!std::isfinite(backingBounds.size.width)
                || !std::isfinite(backingBounds.size.height)
                || !std::isfinite(scale)
                || backingBounds.size.width < 1.0
                || backingBounds.size.height < 1.0
                || scale <= 0.0
                || backingBounds.size.width > std::numeric_limits<std::uint32_t>::max()
                || backingBounds.size.height > std::numeric_limits<std::uint32_t>::max())
            {
                throw Error(ErrorCode::SystemFailure, 0, "The Cocoa window reported invalid backing metrics");
            }

            return {
                static_cast<std::uint32_t>(std::llround(backingBounds.size.width)),
                static_cast<std::uint32_t>(std::llround(backingBounds.size.height)),
                static_cast<float>(scale),
            };
        }
    }

    void installCrashReporterIfRequested()
    {
        const char *value = std::getenv("KZ_CRASH_TRACE");
        if (value && *value)
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
