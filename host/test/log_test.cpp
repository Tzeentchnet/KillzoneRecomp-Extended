#include "kz_host/core.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    constexpr int kRecordCount = 128;

    int childMain(const std::filesystem::path &currentLog, const std::filesystem::path &previousLog)
    {
        if (kz::host::redirectStandardStreams(currentLog, previousLog)
            != kz::host::StandardStreamRedirection::Redirected)
        {
            return 2;
        }
        for (int i = 0; i < kRecordCount; ++i)
        {
            std::printf("stdout-%03d\n", i);
            std::fprintf(stderr, "stderr-%03d\n", i);
        }
        std::_Exit(0);
    }

#ifdef _WIN32
    std::wstring quote(const std::filesystem::path &path)
    {
        return L"\"" + path.wstring() + L"\"";
    }

    int runChild(
        const std::filesystem::path &executable,
        const std::filesystem::path &currentLog,
        const std::filesystem::path &previousLog)
    {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        const HANDLE nullInput = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        const HANDLE nullOutput = CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (nullInput == INVALID_HANDLE_VALUE || nullOutput == INVALID_HANDLE_VALUE)
        {
            if (nullInput != INVALID_HANDLE_VALUE)
                CloseHandle(nullInput);
            if (nullOutput != INVALID_HANDLE_VALUE)
                CloseHandle(nullOutput);
            return 3;
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nullInput;
        startup.hStdOutput = nullOutput;
        startup.hStdError = nullOutput;
        PROCESS_INFORMATION process{};
        std::wstring command =
            quote(executable) + L" --child " + quote(currentLog) + L" " + quote(previousLog);
        const BOOL created = CreateProcessW(
            executable.c_str(),
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process);
        CloseHandle(nullInput);
        CloseHandle(nullOutput);
        if (!created)
            return 4;

        const DWORD wait = WaitForSingleObject(process.hProcess, 30000);
        DWORD exitCode = 5;
        if (wait == WAIT_OBJECT_0)
            GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return static_cast<int>(exitCode);
    }
#else
    int runChild(
        const std::filesystem::path &executable,
        const std::filesystem::path &currentLog,
        const std::filesystem::path &previousLog)
    {
        const pid_t child = fork();
        if (child == -1)
            return 3;
        if (child == 0)
        {
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            execl(
                executable.c_str(),
                executable.c_str(),
                "--child",
                currentLog.c_str(),
                previousLog.c_str(),
                static_cast<char *>(nullptr));
            _exit(127);
        }
        int status = 0;
        if (waitpid(child, &status, 0) == -1 || !WIFEXITED(status))
            return 4;
        return WEXITSTATUS(status);
    }
#endif

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    int fail(const std::string &message)
    {
        std::cerr << "[kz-host-log-test] FAIL: " << message << '\n';
        return 1;
    }
}

int main(int argc, char **argv)
{
    if (argc == 4 && std::string(argv[1]) == "--child")
        return childMain(argv[2], argv[3]);

#ifdef _WIN32
    const auto processId = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto processId = static_cast<unsigned long long>(getpid());
#endif
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("kz-host-log-test-" + std::to_string(processId));
    const std::filesystem::path currentLog = directory / "killzone.log";
    const std::filesystem::path previousLog = directory / "killzone.prev.log";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    error.clear();
    std::filesystem::create_directories(directory, error);
    if (error)
        return fail("cannot create test directory: " + error.message());
    {
        std::ofstream oldLog(currentLog, std::ios::binary);
        oldLog << "previous-run\n";
    }

    const int childResult = runChild(kz::host::executablePath(), currentLog, previousLog);
    if (childResult != 0)
        return fail("child process exited with " + std::to_string(childResult));
    if (readFile(previousLog) != "previous-run\n")
        return fail("previous log was not rotated intact");

    const std::string output = readFile(currentLog);
    for (int i = 0; i < kRecordCount; ++i)
    {
        char stdoutRecord[32]{};
        char stderrRecord[32]{};
        std::snprintf(stdoutRecord, sizeof(stdoutRecord), "stdout-%03d", i);
        std::snprintf(stderrRecord, sizeof(stderrRecord), "stderr-%03d", i);
        for (const char *record : {stdoutRecord, stderrRecord})
        {
            const std::size_t first = output.find(record);
            if (first == std::string::npos)
                return fail(std::string("missing record ") + record);
            if (output.find(record, first + 1) != std::string::npos)
                return fail(std::string("duplicate record ") + record);
        }
    }

    std::filesystem::remove_all(directory, error);
    if (error)
        return fail("cannot remove test directory: " + error.message());
    std::cout << "[kz-host-log-test] passed " << kRecordCount * 2
              << " interleaved records and abrupt child exit\n";
    return 0;
}
