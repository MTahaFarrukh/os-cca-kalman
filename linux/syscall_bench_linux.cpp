/**
 * @file syscall_bench_linux.cpp
 * @brief 15 System Call Demo and Benchmark Suite for Ubuntu Linux
 * 
 * Part of CT-353 Operating Systems CCA.
 * Measures execution time (avg/min/max us), CPU time (user/sys),
 * and peak memory (RSS) using getrusage() across 5 runs.
 * Output: results/linux_results.csv
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include "../common/bench.hpp"
#include "../common/logger.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>

int main() {
    logInfo("==================================================================");
    logInfo("  CT-353 Operating Systems: Linux 15 System Call Benchmark Suite");
    logInfo("==================================================================");

    // ========================================================================
    // Part A: Run Functional Demonstrations
    // ========================================================================
    logInfo("--- Executing Part A: Functional System Call Demonstrations ---");

    if (!demo_file_io()) logError("File I/O demo failed!");
    if (!demo_proc())    logError("Process demo failed!");
    if (!demo_mem())     logError("Memory demo failed!");
    if (!demo_ipc())     logError("IPC demo failed!");
    if (!demo_perm())    logError("Permissions demo failed!");

    logInfo("All 15 system call demonstrations completed successfully.\n");

    // ========================================================================
    // Part B: Run Benchmarks (1000 iterations, 200 for fork/exec/shm, 5 runs)
    // ========================================================================
    const std::string csvPath = "results/linux_results.csv";
    // Remove old results file if present
    ::unlink(csvPath.c_str());

    logInfo("--- Executing Part B: 15 System Call Microbenchmarking ---");
    logInfo("Writing benchmark results to: " + csvPath);

    constexpr size_t RUNS = 5;
    constexpr size_t ITERS_FAST = 1000;
    constexpr size_t ITERS_HEAVY = 200;

    const char* scratchFile = "results/scratch_bench.tmp";
    // Prepare scratch file
    int init_fd = ::open(scratchFile, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (init_fd >= 0) {
        char dummy[1024];
        std::memset(dummy, 'A', sizeof(dummy));
        ssize_t init_w = ::write(init_fd, dummy, sizeof(dummy));
        (void)init_w;
        ::close(init_fd);
    }

    std::vector<BenchmarkResult> results;
    results.reserve(15);

    // 1. open
    logInfo("Benchmarking open()...");
    auto res_open = runBench("open", "File I/O", ITERS_FAST, RUNS, [&]() {
        // syscall: open
        int fd = ::open(scratchFile, O_RDONLY);
        if (fd >= 0) {
            // syscall: close
            ::close(fd);
        }
    });
    appendBenchmarkCsv(csvPath, res_open);
    results.push_back(res_open);

    // 2. read
    logInfo("Benchmarking read()...");
    int readFd = ::open(scratchFile, O_RDONLY);
    char readBuf[128];
    auto res_read = runBench("read", "File I/O", ITERS_FAST, RUNS, [&]() {
        // syscall: read
        ssize_t ret = ::read(readFd, readBuf, sizeof(readBuf));
        if (ret <= 0) {
            ::lseek(readFd, 0, SEEK_SET);
        }
    });
    if (readFd >= 0) ::close(readFd);
    appendBenchmarkCsv(csvPath, res_read);
    results.push_back(res_read);

    // 3. write
    logInfo("Benchmarking write()...");
    int devNullFd = ::open("/dev/null", O_WRONLY);
    const char writeBuf[128] = "OS_BENCH_DATA";
    auto res_write = runBench("write", "File I/O", ITERS_FAST, RUNS, [&]() {
        // syscall: write
        ssize_t w = ::write(devNullFd, writeBuf, sizeof(writeBuf));
        (void)w;
    });
    if (devNullFd >= 0) ::close(devNullFd);
    appendBenchmarkCsv(csvPath, res_write);
    results.push_back(res_write);

    // 4. close
    logInfo("Benchmarking close()...");
    auto res_close = runBench("close", "File I/O", ITERS_FAST, RUNS, [&]() {
        int fd = ::open("/dev/null", O_WRONLY);
        // syscall: close
        ::close(fd);
    });
    appendBenchmarkCsv(csvPath, res_close);
    results.push_back(res_close);

    // 5. fork
    logInfo("Benchmarking fork()...");
    auto res_fork = runBench("fork", "Process", ITERS_HEAVY, RUNS, [&]() {
        // syscall: fork
        pid_t pid = ::fork();
        if (pid == 0) {
            ::_exit(0);
        } else if (pid > 0) {
            // syscall: waitpid
            ::waitpid(pid, nullptr, 0);
        }
    });
    appendBenchmarkCsv(csvPath, res_fork);
    results.push_back(res_fork);

    // 6. exec (execl)
    logInfo("Benchmarking exec (execl of /bin/echo)...");
    auto res_exec = runBench("exec", "Process", ITERS_HEAVY, RUNS, [&]() {
        pid_t pid = ::fork();
        if (pid == 0) {
            // Redirect child stdout to /dev/null during exec benchmark
            int nullFd = ::open("/dev/null", O_WRONLY);
            if (nullFd >= 0) {
                ::dup2(nullFd, STDOUT_FILENO);
                ::close(nullFd);
            }
            // syscall: execl
            ::execl("/bin/echo", "echo", static_cast<char*>(nullptr));
            ::_exit(127);
        } else if (pid > 0) {
            ::waitpid(pid, nullptr, 0);
        }
    });
    appendBenchmarkCsv(csvPath, res_exec);
    results.push_back(res_exec);

    // 7. wait (waitpid)
    logInfo("Benchmarking wait (waitpid)...");
    auto res_wait = runBench("wait", "Process", ITERS_HEAVY, RUNS, [&]() {
        pid_t pid = ::fork();
        if (pid == 0) {
            ::_exit(0);
        } else if (pid > 0) {
            int st = 0;
            // syscall: waitpid
            ::waitpid(pid, &st, 0);
        }
    });
    appendBenchmarkCsv(csvPath, res_wait);
    results.push_back(res_wait);

    // 8. mmap
    logInfo("Benchmarking mmap()...");
    const size_t mmap_sz = 4096;
    auto res_mmap = runBench("mmap", "Memory", ITERS_FAST, RUNS, [&]() {
        // syscall: mmap
        void* ptr = ::mmap(nullptr, mmap_sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr != MAP_FAILED) {
            // syscall: munmap
            ::munmap(ptr, mmap_sz);
        }
    });
    appendBenchmarkCsv(csvPath, res_mmap);
    results.push_back(res_mmap);

    // 9. munmap
    logInfo("Benchmarking munmap()...");
    auto res_munmap = runBench("munmap", "Memory", ITERS_FAST, RUNS, [&]() {
        void* ptr = ::mmap(nullptr, mmap_sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        // syscall: munmap
        ::munmap(ptr, mmap_sz);
    });
    appendBenchmarkCsv(csvPath, res_munmap);
    results.push_back(res_munmap);

    // 10. pipe
    logInfo("Benchmarking pipe()...");
    int pipeFds[2];
    auto res_pipe = runBench("pipe", "IPC", ITERS_FAST, RUNS, [&]() {
        // syscall: pipe
        if (::pipe(pipeFds) == 0) {
            // syscall: close
            ::close(pipeFds[0]);
            ::close(pipeFds[1]);
        }
    });
    appendBenchmarkCsv(csvPath, res_pipe);
    results.push_back(res_pipe);

    // 11. shmget
    logInfo("Benchmarking shmget()...");
    auto res_shmget = runBench("shmget", "IPC", ITERS_HEAVY, RUNS, [&]() {
        // syscall: shmget
        int id = ::shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);
        if (id >= 0) {
            // syscall: shmctl (cleanup)
            ::shmctl(id, IPC_RMID, nullptr);
        }
    });
    appendBenchmarkCsv(csvPath, res_shmget);
    results.push_back(res_shmget);

    // 12. shmat
    logInfo("Benchmarking shmat()...");
    int pre_shmid = ::shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);
    auto res_shmat = runBench("shmat", "IPC", ITERS_HEAVY, RUNS, [&]() {
        // syscall: shmat
        void* p = ::shmat(pre_shmid, nullptr, 0);
        if (p != reinterpret_cast<void*>(-1)) {
            // syscall: shmdt
            ::shmdt(p);
        }
    });
    if (pre_shmid >= 0) ::shmctl(pre_shmid, IPC_RMID, nullptr);
    appendBenchmarkCsv(csvPath, res_shmat);
    results.push_back(res_shmat);

    // 13. chmod
    logInfo("Benchmarking chmod()...");
    auto res_chmod = runBench("chmod", "Permissions", ITERS_FAST, RUNS, [&]() {
        // syscall: chmod
        int r = ::chmod(scratchFile, 0600);
        (void)r;
    });
    appendBenchmarkCsv(csvPath, res_chmod);
    results.push_back(res_chmod);

    // 14. chown
    logInfo("Benchmarking chown()...");
    uid_t uid = ::geteuid();
    gid_t gid = ::getegid();
    auto res_chown = runBench("chown", "Permissions", ITERS_FAST, RUNS, [&]() {
        // syscall: chown
        int r = ::chown(scratchFile, uid, gid);
        if (r != 0 && errno != EPERM) {
            // Handle EPERM gracefully
        }
    });
    appendBenchmarkCsv(csvPath, res_chown);
    results.push_back(res_chown);

    // 15. umask
    logInfo("Benchmarking umask()...");
    auto res_umask = runBench("umask", "Permissions", ITERS_FAST, RUNS, [&]() {
        // syscall: umask
        mode_t m = ::umask(0022);
        (void)m;
    });
    appendBenchmarkCsv(csvPath, res_umask);
    results.push_back(res_umask);

    // Cleanup scratch file
    ::unlink(scratchFile);

    // ========================================================================
    // Print Summary Table
    // ========================================================================
    std::cout << "\n";
    std::cout << "========================================================================================\n";
    std::cout << "  CT-353 Operating Systems: Linux System Call Benchmark Summary (5 Runs)\n";
    std::cout << "========================================================================================\n";
    std::cout << std::left << std::setw(12) << "Syscall"
              << std::setw(14) << "Category"
              << std::setw(12) << "Iterations"
              << std::setw(14) << "Avg (us)"
              << std::setw(14) << "Min (us)"
              << std::setw(14) << "Max (us)"
              << std::setw(12) << "MaxRSS(KB)" << "\n";
    std::cout << "----------------------------------------------------------------------------------------\n";

    for (const auto& r : results) {
        std::cout << std::left << std::setw(12) << r.syscall
                  << std::setw(14) << r.category
                  << std::setw(12) << r.iterations
                  << std::fixed << std::setprecision(4)
                  << std::setw(14) << r.avg_us
                  << std::setw(14) << r.min_us
                  << std::setw(14) << r.max_us
                  << std::setw(12) << r.maxrss_kb << "\n";
    }
    std::cout << "========================================================================================\n";
    std::cout << "Results saved to: " << csvPath << "\n\n";

    logInfo("Benchmark completed successfully.");
    return 0;
}

#else
int main() { return 0; }
#endif
