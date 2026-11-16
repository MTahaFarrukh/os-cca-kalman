#include "../common/bench.hpp"
#include "../common/logger.hpp"
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

/**
 * @brief Interrogates Win32 process timing and memory counters.
 * Uses GetProcessTimes (user + kernel CPU) and GetProcessMemoryInfo (PeakWorkingSetSize).
 */
ResourceUsage getPlatformResourceUsage() {
    ResourceUsage usage{};
    FILETIME ftCreation{}, ftExit{}, ftKernel{}, ftUser{};

    if (GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
        ULARGE_INTEGER uUser{}, uKernel{};
        uUser.LowPart = ftUser.dwLowDateTime;
        uUser.HighPart = ftUser.dwHighDateTime;
        uKernel.LowPart = ftKernel.dwLowDateTime;
        uKernel.HighPart = ftKernel.dwHighDateTime;

        // FILETIME units are 100-nanosecond slices (0.1 microsecond)
        usage.cpu_user_us = static_cast<double>(uUser.QuadPart) / 10.0;
        usage.cpu_sys_us = static_cast<double>(uKernel.QuadPart) / 10.0;
    } else {
        logSysError("GetProcessTimes");
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        usage.maxrss_kb = static_cast<int64_t>(pmc.PeakWorkingSetSize / 1024);
    } else {
        logSysError("GetProcessMemoryInfo");
    }

    return usage;
}

/**
 * @brief Win32 process demonstration: CreateProcess + WaitForSingleObject.
 * Launches this executable again with --noop, waits for it and reports its exit code.
 */
void demo_proc() {
    logInfo("=== Win32 Process Demo (CreateProcess / WaitForSingleObject) ===");

    char exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        logSysError("GetModuleFileNameA");
        return;
    }

    std::string cmd = std::string("\"") + exePath + "\" --noop";
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // CreateProcessA - create a new child process (fork + exec in one call)
    if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        logSysError("CreateProcessA");
        return;
    }
    logInfo("CreateProcessA: child process started, pid=" + std::to_string(pi.dwProcessId));

    // WaitForSingleObject - block until the child process has exited (waitpid)
    const DWORD w = WaitForSingleObject(pi.hProcess, 10000);
    if (w != WAIT_OBJECT_0) {
        logSysError("WaitForSingleObject");
        TerminateProcess(pi.hProcess, 1);
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        logInfo("WaitForSingleObject: child exited normally with code " + std::to_string(code));
    }

    CloseHandle(pi.hProcess);   // CloseHandle - release process handle
    CloseHandle(pi.hThread);    // CloseHandle - release thread handle
    logInfo("Process demo complete");
}

#endif
