// Standalone Windows wait-timing probe; this does not exercise the game scheduler.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>

static void measure(const char* mode, int timeout_ms)
{
    using namespace std::chrono;
    std::mutex mutex;
    std::condition_variable cv;
    std::unique_lock lock(mutex);
    std::vector<double> elapsed;
    for (int i = 0; i < 55; ++i)
    {
        const auto start = steady_clock::now();
        cv.wait_until(lock, start + milliseconds(timeout_ms), [] { return false; });
        const auto stop = steady_clock::now();
        if (i >= 5)
            elapsed.push_back(duration<double, std::milli>(stop - start).count());
    }
    std::sort(elapsed.begin(), elapsed.end());
    const auto late = std::count_if(elapsed.begin(), elapsed.end(),
                                    [timeout_ms](double value) { return value > timeout_ms + 5.0; });
    std::printf("%s target=%d ms n=%zu median=%.3f ms p95=%.3f ms >target+5ms=%td\n",
                mode, timeout_ms, elapsed.size(), elapsed[elapsed.size() / 2],
                elapsed[(elapsed.size() * 95 + 99) / 100 - 1], late);
}

static bool measureHighResolution(HANDLE timer, int timeout_ms)
{
    using namespace std::chrono;
    std::vector<double> elapsed;
    for (int i = 0; i < 55; ++i)
    {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(timeout_ms) * 10000;
        const auto start = steady_clock::now();
        if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0)
        {
            std::fprintf(stderr, "high-resolution timer failed: %lu\n", GetLastError());
            return false;
        }
        const auto stop = steady_clock::now();
        if (i >= 5)
            elapsed.push_back(duration<double, std::milli>(stop - start).count());
    }
    std::sort(elapsed.begin(), elapsed.end());
    const auto late = std::count_if(elapsed.begin(), elapsed.end(),
                                    [timeout_ms](double value) { return value > timeout_ms + 5.0; });
    std::printf("high-res timer target=%d ms n=%zu median=%.3f ms p95=%.3f ms >target+5ms=%td\n",
                timeout_ms, elapsed.size(), elapsed[elapsed.size() / 2],
                elapsed[(elapsed.size() * 95 + 99) / 100 - 1], late);
    return true;
}

int main()
{
    for (int duration : {1, 2, 5, 10})
        measure("default", duration);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer)
    {
        std::fprintf(stderr, "CreateWaitableTimerEx failed: %lu\n", GetLastError());
        return 1;
    }
    for (int duration : {1, 2, 5, 10})
    {
        if (!measureHighResolution(timer, duration))
        {
            CloseHandle(timer);
            return 1;
        }
    }
    CloseHandle(timer);
    const MMRESULT result = timeBeginPeriod(1);
    if (result != TIMERR_NOERROR)
    {
        std::fprintf(stderr, "timeBeginPeriod failed: %u\n", result);
        return 1;
    }
    for (int duration : {1, 2, 5, 10})
        measure("period-requested", duration);
    if (timeEndPeriod(1) != TIMERR_NOERROR)
    {
        std::fprintf(stderr, "timeEndPeriod failed\n");
        return 1;
    }
    return 0;
}
