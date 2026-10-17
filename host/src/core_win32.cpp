#include "kz_host/core.h"

#include "internal.h"

#include <windows.h>
#include <intrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    [[nodiscard]] DWORD nativeProtection(kz::host::MemoryProtection protection)
    {
        using kz::host::MemoryProtection;
        switch (protection)
        {
        case MemoryProtection::NoAccess:
            return PAGE_NOACCESS;
        case MemoryProtection::ReadOnly:
            return PAGE_READONLY;
        case MemoryProtection::ReadWrite:
            return PAGE_READWRITE;
        case MemoryProtection::ReadExecute:
            return PAGE_EXECUTE_READ;
        case MemoryProtection::ReadWriteExecute:
            return PAGE_EXECUTE_READWRITE;
        }
        throw kz::host::Error(kz::host::ErrorCode::InvalidArgument, 0, "Unknown host memory protection");
    }

    [[noreturn]] void throwWin32(const std::string &operation, DWORD error = GetLastError())
    {
        throw kz::host::Error(
            kz::host::ErrorCode::SystemFailure,
            error,
            operation + " failed with Win32 error " + std::to_string(error));
    }

    [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
    {
        if (text.empty())
            return {};
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw kz::host::Error(kz::host::ErrorCode::InvalidArgument, 0, "UTF-8 host string is too long");
        const int inputSize = static_cast<int>(text.size());
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
        if (size == 0)
            throwWin32("Converting a UTF-8 host string");
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, result.data(), size) == 0)
            throwWin32("Converting a UTF-8 host string");
        return result;
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
        void *result = VirtualAlloc(address, size, MEM_RESERVE | MEM_COMMIT, nativeProtection(protection));
        if (!result)
            throwWin32("Allocating host memory");
        if (placement == MemoryPlacement::ExactAddress && result != address)
        {
            VirtualFree(result, 0, MEM_RELEASE);
            throw Error(ErrorCode::SystemFailure, 0, "Windows did not honor an exact host memory address");
        }
        return result;
    }

    void releaseMemory(void *data, std::size_t) noexcept
    {
        if (!VirtualFree(data, 0, MEM_RELEASE))
            std::fprintf(stderr, "[kz-host] VirtualFree failed with Win32 error %lu\n", GetLastError());
    }

    void protectMemory(void *data, std::size_t size, MemoryProtection protection)
    {
        DWORD previous = 0;
        if (!VirtualProtect(data, size, nativeProtection(protection), &previous))
            throwWin32("Protecting host memory");
    }

    void flushInstructionCache(void *data, std::size_t size)
    {
        if (!FlushInstructionCache(GetCurrentProcess(), data, size))
            throwWin32("Flushing the host instruction cache");
    }
}

namespace kz::host
{
    std::size_t memoryPageSize()
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        if (info.dwPageSize == 0)
            throw Error(ErrorCode::SystemFailure, 0, "Windows reported a zero host memory page size");
        return info.dwPageSize;
    }

    std::size_t memoryAllocationGranularity()
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        if (info.dwAllocationGranularity == 0)
            throw Error(ErrorCode::SystemFailure, 0, "Windows reported a zero host allocation granularity");
        return info.dwAllocationGranularity;
    }

    std::filesystem::path executablePath()
    {
        std::vector<wchar_t> path(512);
        for (;;)
        {
            SetLastError(ERROR_SUCCESS);
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
                throwWin32("Resolving the executable path");
            if (length < path.size() - 1)
                return std::filesystem::path(std::wstring_view(path.data(), length));
            if (path.size() >= 32768)
                throw Error(ErrorCode::SystemFailure, ERROR_INSUFFICIENT_BUFFER, "The executable path exceeds 32,767 characters");
            path.resize(path.size() * 2);
        }
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
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        const DWORD type = output != nullptr && output != INVALID_HANDLE_VALUE ? GetFileType(output) : FILE_TYPE_UNKNOWN;
        if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE)
            return StandardStreamRedirection::AlreadyRedirected;

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

        if (!_wfreopen(currentLog.c_str(), L"w", stdout))
            throw Error(ErrorCode::SystemFailure, errno, "Redirecting stdout to the host log failed");
        if (!_wfreopen(currentLog.c_str(), L"a", stderr))
            throw Error(ErrorCode::SystemFailure, errno, "Redirecting stderr to the host log failed");
        std::setvbuf(stdout, nullptr, _IOLBF, 4096);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        std::ios::sync_with_stdio(true);

        DWORD processIds[2]{};
        if (GetConsoleProcessList(processIds, 2) == 1 && !FreeConsole())
            throwWin32("Detaching the private console");
        return StandardStreamRedirection::Redirected;
    }

    void setEnvironment(std::string_view name, std::string_view value)
    {
        if (name.empty() || name.find('=') != std::string_view::npos)
            throw Error(ErrorCode::InvalidArgument, 0, "Host environment variable name is invalid");
        const std::string nameString(name);
        const std::string valueString(value);
        const int result = _putenv_s(nameString.c_str(), valueString.c_str());
        if (result != 0)
            throw Error(ErrorCode::SystemFailure, static_cast<std::uint64_t>(result), "Setting host environment variable '" + nameString + "' failed");
    }

    CpuFeatures cpuFeatures()
    {
#if defined(_M_X64) || defined(__x86_64__)
        CpuFeatures features;
        features.architecture = CpuArchitecture::X86_64;
        features.simd = SimdLevel::Sse2;

        int registers[4]{};
        __cpuid(registers, 0);
        const int maxLeaf = registers[0];
        if (maxLeaf < 1)
            return features;
        __cpuidex(registers, 1, 0);
        const bool sse41 = (registers[2] & (1 << 19)) != 0;
        const bool osxsave = (registers[2] & (1 << 27)) != 0;
        const bool avxHardware = (registers[2] & (1 << 28)) != 0;
        const bool avxOs = osxsave && (_xgetbv(0) & 0x6) == 0x6;
        if (sse41)
            features.simd = SimdLevel::Sse41;
        if (avxHardware && avxOs)
        {
            features.simd = SimdLevel::Avx;
            features.fma = (registers[2] & (1 << 12)) != 0;
        }
        if (maxLeaf >= 7)
        {
            __cpuidex(registers, 7, 0);
            if (features.simd == SimdLevel::Avx && (registers[1] & (1 << 5)) != 0)
                features.simd = SimdLevel::Avx2;
            features.bmi2 = (registers[1] & (1 << 8)) != 0;
        }
        return features;
#elif defined(_M_ARM64) || defined(__aarch64__)
        return {CpuArchitecture::Arm64, SimdLevel::Neon, false, false};
#else
        return {};
#endif
    }

    void configureGuestFloatingPoint()
    {
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(_mm_getcsr() | 0x8040u);
#else
        throw Error(ErrorCode::Unsupported, 0, "PS2 guest floating-point setup is not implemented for this Windows architecture");
#endif
    }

    void setCurrentThreadName(std::string_view name)
    {
        if (name.empty())
            throw Error(ErrorCode::InvalidArgument, 0, "Host thread name cannot be empty");
        const std::wstring wideName = utf8ToWide(name);
        const HRESULT result = SetThreadDescription(GetCurrentThread(), wideName.c_str());
        if (FAILED(result))
            throw Error(ErrorCode::SystemFailure, static_cast<std::uint32_t>(result), "Setting the host thread name failed");
    }

    WindowMetrics queryWindowMetrics(const NativeWindow &window)
    {
        if (window.type != NativeWindowType::Win32)
            throw Error(ErrorCode::Unsupported, 0, "Window metrics require a Win32 native window on the Windows host");
        if (!window.window)
            throw Error(ErrorCode::InvalidArgument, 0, "Window metrics require a non-null HWND");
        RECT rect{};
        const HWND hwnd = static_cast<HWND>(window.window);
        if (!GetClientRect(hwnd, &rect))
            throwWin32("Querying Win32 window metrics");
        const UINT dpi = GetDpiForWindow(hwnd);
        return {
            static_cast<std::uint32_t>(std::max<LONG>(rect.right - rect.left, 1)),
            static_cast<std::uint32_t>(std::max<LONG>(rect.bottom - rect.top, 1)),
            dpi ? static_cast<float>(dpi) / 96.0f : 1.0f,
        };
    }
}
