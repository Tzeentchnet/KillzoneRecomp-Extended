#include "kz_host/core.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

namespace
{
    int fail(const char *message)
    {
        std::fprintf(stderr, "[kz-host-test] FAIL: %s\n", message);
        return 1;
    }
}

int main()
{
    try
    {
        const std::filesystem::path executable = kz::host::executablePath();
        if (executable.empty() || !std::filesystem::exists(executable))
            return fail("executable path is missing");
        if (kz::host::executableDirectory() != executable.parent_path())
            return fail("executable directory does not match the executable path");
        if (kz::host::pathToUtf8(executable).empty())
            return fail("UTF-8 executable path is empty");
        if (kz::host::memoryPageSize() < 4096)
            return fail("host page size is unexpectedly small");
        if (kz::host::memoryAllocationGranularity() < kz::host::memoryPageSize())
            return fail("host allocation granularity is smaller than its page size");

        kz::host::setEnvironment("KZ_HOST_CORE_TEST", "ok");
        const char *environment = std::getenv("KZ_HOST_CORE_TEST");
        if (!environment || std::strcmp(environment, "ok") != 0)
            return fail("environment update was not observable");

        kz::host::ManualResetEvent event;
        std::atomic<bool> waiterFinished{false};
        std::thread waiter([&] {
            event.wait();
            waiterFinished = true;
        });
        const bool signaledEarly = event.waitFor(std::chrono::milliseconds(1));
        event.signal();
        waiter.join();
        if (signaledEarly)
            return fail("unsignaled manual-reset event reported success");
        if (!waiterFinished || !event.waitFor(std::chrono::milliseconds(1)))
            return fail("manual-reset event did not release or remain signaled");
        event.reset();

        const auto sleepStart = kz::host::monotonicNow();
        kz::host::sleepFor(std::chrono::milliseconds(1));
        if (kz::host::monotonicNow() <= sleepStart)
            return fail("monotonic time did not advance");

        const kz::host::CpuFeatures features = kz::host::cpuFeatures();
        if (features.architecture == kz::host::CpuArchitecture::Unknown)
            return fail("host CPU architecture is unknown");
        kz::host::configureGuestFloatingPoint();

        auto code = kz::host::MemoryMapping::allocate(
            kz::host::memoryPageSize(),
            kz::host::MemoryProtection::ReadWrite);
        if (!code || !code.data())
            return fail("host memory mapping is empty");

        if (features.architecture == kz::host::CpuArchitecture::X86_64)
        {
            const std::uint8_t return42[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
            std::memcpy(code.data(), return42, sizeof(return42));
            code.protect(kz::host::MemoryProtection::ReadExecute);
            code.flushInstructionCache(0, sizeof(return42));
            using TestFunction = int (*)();
            if (reinterpret_cast<TestFunction>(code.data())() != 42)
                return fail("W^X executable-memory smoke function returned the wrong value");
        }
        else if (features.architecture == kz::host::CpuArchitecture::Arm64)
        {
            const std::uint32_t return42[] = {0x52800540u, 0xd65f03c0u};
            std::memcpy(code.data(), return42, sizeof(return42));
            code.protect(kz::host::MemoryProtection::ReadExecute);
            code.flushInstructionCache(0, sizeof(return42));
            using TestFunction = int (*)();
            if (reinterpret_cast<TestFunction>(code.data())() != 42)
                return fail("W^X executable-memory smoke function returned the wrong value");
        }
        else
        {
            code.protect(kz::host::MemoryProtection::ReadOnly);
        }

        kz::host::setCurrentThreadName("kz-host-test");
        std::printf(
            "[kz-host-test] passed (arch=%d simd=%d page=%zu)\n",
            static_cast<int>(features.architecture),
            static_cast<int>(features.simd),
            kz::host::memoryPageSize());
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "[kz-host-test] FAIL: %s\n", error.what());
        return 1;
    }
}
