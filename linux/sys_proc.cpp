/**
 * @file sys_proc.cpp
 * @brief Raw POSIX Process system calls: fork, exec (execl), wait (waitpid), getrusage
 * 
 * Part of CT-353 Operating Systems CCA.
 * Implements process creation, execution replacement, and process synchronization.
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include "../common/bench.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

/**
 * @brief Interrogates Linux process resource usage using getrusage(RUSAGE_SELF).
 * Measures user CPU time, system CPU time, and peak resident set size (ru_maxrss).
 */
ResourceUsage getPlatformResourceUsage() {
    ResourceUsage usage{};
    struct rusage ru{};

    // raw syscall: getrusage
    if (::getrusage(RUSAGE_SELF, &ru) == 0) {
        usage.cpu_user_us = static_cast<double>(ru.ru_utime.tv_sec) * 1000000.0 +
                            static_cast<double>(ru.ru_utime.tv_usec);
        usage.cpu_sys_us = static_cast<double>(ru.ru_stime.tv_sec) * 1000000.0 +
                           static_cast<double>(ru.ru_stime.tv_usec);
        usage.maxrss_kb = static_cast<int64_t>(ru.ru_maxrss); // ru_maxrss is in KB on Linux
    } else {
        logSysError("getrusage");
    }

    return usage;
}

bool demo_proc() {
    logInfo("[DEMO] Starting Process system calls demonstration (fork, execl, waitpid)...");

    // 1. Raw syscall: fork
    // syscall: fork
    pid_t pid = ::fork();
    if (pid < 0) {
        logSysError("fork");
        return false;
    }

    if (pid == 0) {
        // Child process
        // 2. Raw syscall: execl
        // syscall: execl
        ::execl("/bin/echo", "echo", "CCA_CT353_FORK_EXEC_DEMO_OK", static_cast<char*>(nullptr));

        // If execl returns, it failed
        logSysError("execl");
        ::_exit(127);
    }

    // Parent process
    int status = 0;
    // 3. Raw syscall: waitpid
    // syscall: waitpid
    pid_t waited_pid = ::waitpid(pid, &status, 0);
    if (waited_pid < 0) {
        logSysError("waitpid");
        return false;
    }

    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        logInfo("[DEMO] Child process " + std::to_string(pid) + " exited normally with status " +
                std::to_string(exitCode));
        return (exitCode == 0);
    }

    logError("[DEMO] Child process " + std::to_string(pid) + " did not terminate normally.");
    return false;
}

#endif // _WIN32
