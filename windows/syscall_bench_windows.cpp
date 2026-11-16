/**
 * @file syscall_bench_windows.cpp
 * @brief Win32 API benchmark driver (Part B).
 *
 * Benchmarks 15 Win32 calls mirroring the Linux syscall benchmark.
 * Uses bench.hpp runBench() harness (1000 iters, 200 for CreateProcess, 5 runs).
 * Outputs results/windows_results.csv with an extra "linux_equivalent" column.
 *
 * Resource metrics: CPU time via GetProcessTimes, peak RSS via GetProcessMemoryInfo
 * (implemented in sys_proc_win.cpp / getPlatformResourceUsage).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <psapi.h>
#ifdef ERROR
#undef ERROR
#endif

#include "../common/bench.hpp"
#include "../common/logger.hpp"
#include "sys_win.hpp"

#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <cstdio>
#include <cstring>

// Forward declarations from demo files
void demo_file_io();
void demo_proc();
void demo_mem();
void demo_ipc();
void demo_perm();

// ─────────────────────────────────────────────────────────────────────────────
// Extended result with linux_equivalent mapping
// ─────────────────────────────────────────────────────────────────────────────
struct WinBenchResult {
    BenchmarkResult base;
    std::string linux_equiv;
};

// ─────────────────────────────────────────────────────────────────────────────
// Custom CSV writer that inserts linux_equivalent as column 2
// Header: syscall,linux_equivalent,category,iterations,avg_us,min_us,max_us,
//         cpu_user_us,cpu_sys_us,maxrss_kb
// ─────────────────────────────────────────────────────────────────────────────
static bool appendWinCsv(const std::string& csvPath, const WinBenchResult& r) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(csvPath);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);

    bool is_empty = !fs::exists(p, ec) || fs::file_size(p, ec) == 0;

    std::ofstream out(csvPath, std::ios::out | std::ios::app);
    if (!out.is_open()) return false;

    if (is_empty) {
        out << "syscall,linux_equivalent,category,iterations,"
               "avg_us,min_us,max_us,cpu_user_us,cpu_sys_us,maxrss_kb\n";
    }

    out << std::fixed << std::setprecision(4);
    out << r.base.syscall     << ","
        << r.linux_equiv      << ","
        << r.base.category    << ","
        << r.base.iterations  << ","
        << r.base.avg_us      << ","
        << r.base.min_us      << ","
        << r.base.max_us      << ","
        << r.base.cpu_user_us << ","
        << r.base.cpu_sys_us  << ","
        << r.base.maxrss_kb   << "\n";

    out.flush();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: get own exe path for CreateProcess
// ─────────────────────────────────────────────────────────────────────────────
static std::string getExePath() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::string(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// BENCHMARK FUNCTIONS
// ─────────────────────────────────────────────────────────────────────────────

// 1. CreateFile  (≈ open)
static WinBenchResult bench_CreateFile() {
    const char* path = "data\\bench_cf.tmp";
    auto res = runBench("CreateFile", "File I/O", 1000, 5, [&]() {
        // CreateFileA – create/truncate file for writing (Win32)
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h); // CloseHandle – Win32
    });
    DeleteFileA(path);
    return { res, "open" };
}

// 2. ReadFile  (≈ read)
static WinBenchResult bench_ReadFile() {
    const char* path = "data\\bench_rf.tmp";
    // Create file with content first
    {
        HandleGuard hW(CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (hW.valid()) {
            char buf[4096] = {};
            DWORD wr = 0;
            WriteFile(hW.get(), buf, sizeof(buf), &wr, nullptr);
        }
    }
    auto res = runBench("ReadFile", "File I/O", 1000, 5, [&]() {
        // CreateFileA – open for reading (Win32)
        HandleGuard hR(CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (hR.valid()) {
            char buf[4096];
            DWORD rd = 0;
            // ReadFile – read bytes from file (Win32)
            ReadFile(hR.get(), buf, sizeof(buf), &rd, nullptr);
            // hR destructor calls CloseHandle
        }
    });
    DeleteFileA(path);
    return { res, "read" };
}

// 3. WriteFile  (≈ write)
static WinBenchResult bench_WriteFile() {
    HandleGuard hW(CreateFileA("data\\bench_wf.tmp", GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    char buf[64] = "bench data";
    auto res = runBench("WriteFile", "File I/O", 1000, 5, [&]() {
        DWORD wr = 0;
        // WriteFile – write bytes to file (Win32)
        WriteFile(hW.get(), buf, 10, &wr, nullptr);
    });
    hW.close(); // CloseHandle
    DeleteFileA("data\\bench_wf.tmp");
    return { res, "write" };
}

// 4. CloseHandle  (≈ close)
static WinBenchResult bench_CloseHandle() {
    auto res = runBench("CloseHandle", "File I/O", 1000, 5, [&]() {
        // CreateFileA to get a handle to close (Win32)
        HANDLE h = CreateFileA("data\\bench_ch.tmp", GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            // CloseHandle – release Win32 HANDLE (Win32)
            CloseHandle(h);
        }
    });
    DeleteFileA("data\\bench_ch.tmp");
    return { res, "close" };
}

// 5. CreateProcess  (≈ fork+exec)
static WinBenchResult bench_CreateProcess() {
    std::string exePath = getExePath();
    // Build command line for a no-op: run ourselves with --noop flag
    std::string cmdLine = "\"" + exePath + "\" --noop";
    std::vector<char> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back('\0');

    auto res = runBench("CreateProcess", "Process", 200, 5, [&]() {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        // CreateProcessA – spawn child process (Win32)
        if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                           FALSE, 0, nullptr, nullptr, &si, &pi)) {
            // WaitForSingleObject – wait for child to finish (Win32)
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess); // CloseHandle – Win32
            CloseHandle(pi.hThread);  // CloseHandle – Win32
        }
    });
    return { res, "fork+exec" };
}

// 6. WaitForSingleObject  (≈ waitpid)
// Benchmark: create a child that exits immediately, measure the wait
static WinBenchResult bench_WaitForSingleObject() {
    std::string exePath = getExePath();
    std::string cmdLine = "\"" + exePath + "\" --noop";
    std::vector<char> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back('\0');

    auto res = runBench("WaitForSingleObject", "Process", 200, 5, [&]() {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                           FALSE, 0, nullptr, nullptr, &si, &pi)) {
            // WaitForSingleObject – block until process terminates (Win32)
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    });
    return { res, "waitpid" };
}

// 7. VirtualAlloc  (≈ mmap)
static WinBenchResult bench_VirtualAlloc() {
    auto res = runBench("VirtualAlloc", "Memory", 1000, 5, [&]() {
        // VirtualAlloc – reserve + commit anonymous memory pages (Win32)
        void* p = VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) {
            VirtualFree(p, 0, MEM_RELEASE); // VirtualFree – Win32 release
        }
    });
    return { res, "mmap" };
}

// 8. VirtualFree  (≈ munmap)
static WinBenchResult bench_VirtualFree() {
    auto res = runBench("VirtualFree", "Memory", 1000, 5, [&]() {
        void* p = VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) {
            // VirtualFree – release virtual address region (Win32)
            VirtualFree(p, 0, MEM_RELEASE);
        }
    });
    return { res, "munmap" };
}

// 9. CreatePipe  (≈ pipe)
static WinBenchResult bench_CreatePipe() {
    auto res = runBench("CreatePipe", "IPC", 1000, 5, [&]() {
        HANDLE hR = nullptr, hW = nullptr;
        // CreatePipe – create anonymous byte-stream pipe (Win32)
        if (CreatePipe(&hR, &hW, nullptr, 0)) {
            CloseHandle(hR); // CloseHandle – Win32
            CloseHandle(hW); // CloseHandle – Win32
        }
    });
    return { res, "pipe" };
}

// 10. CreateFileMapping  (≈ shmget)
static WinBenchResult bench_CreateFileMapping() {
    int idx = 0;
    auto res = runBench("CreateFileMapping", "IPC", 1000, 5, [&]() {
        char name[64];
        snprintf(name, sizeof(name), "Local\\CCA_Bench_%lu_%d", static_cast<unsigned long>(GetCurrentProcessId()), idx++);
        // CreateFileMappingA – create named pagefile-backed mapping object (Win32)
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                      PAGE_READWRITE, 0, 4096, name);
        if (h) CloseHandle(h); // CloseHandle – Win32
    });
    return { res, "shmget" };
}

// 11. MapViewOfFile  (≈ shmat)
static WinBenchResult bench_MapViewOfFile() {
    // Keep one mapping open during the benchmark
    HandleGuard hMap(CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0, 4096, "Local\\CCA_BenchMap"));
    auto res = runBench("MapViewOfFile", "IPC", 1000, 5, [&]() {
        // MapViewOfFile – map a view of the file mapping (Win32)
        void* view = MapViewOfFile(hMap.get(), FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (view) {
            // UnmapViewOfFile – remove view from address space (Win32)
            UnmapViewOfFile(view);
        }
    });
    return { res, "shmat" };
}

// 12. UnmapViewOfFile  (≈ shmdt)
static WinBenchResult bench_UnmapViewOfFile() {
    HandleGuard hMap(CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0, 4096, "Local\\CCA_BenchUnmap"));
    auto res = runBench("UnmapViewOfFile", "IPC", 1000, 5, [&]() {
        void* view = MapViewOfFile(hMap.get(), FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (view) {
            // UnmapViewOfFile – unmap the view (Win32)
            UnmapViewOfFile(view);
        }
    });
    return { res, "shmdt" };
}

// 13. SetFileSecurity (DACL)  (≈ chmod)
static WinBenchResult bench_SetFileSecurity() {
    const char* path = "data\\bench_perm.tmp";
    // Create a file to apply DACL to
    {
        HandleGuard h(CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    }

    auto res = runBench("SetFileSecurity", "Permissions", 1000, 5, [&]() {
        PSECURITY_DESCRIPTOR pSD = nullptr;
        // ConvertStringSecurityDescriptorToSecurityDescriptorA – build DACL from SDDL (Win32/advapi32)
        if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
                "D:(A;;GA;;;WD)", SDDL_REVISION_1, &pSD, nullptr)) {
            // SetFileSecurityA – apply DACL to file (Win32/advapi32)
            SetFileSecurityA(path, DACL_SECURITY_INFORMATION, pSD);
            LocalFree(pSD); // LocalFree – Win32 heap release
        }
    });
    DeleteFileA(path);
    return { res, "chmod" };
}

// 14. SetFileSecurity (Owner/SACL via SetNamedSecurityInfoA)  (≈ chown)
// Note: SetNamedSecurityInfoA is the Win32 analog to chown for changing owner;
// SetFileSecurityA only sets DACL/SACL, not owner (requires SeRestorePrivilege).
// We benchmark SetNamedSecurityInfoA with DACL_SECURITY_INFORMATION to stay
// in the same privilege space (gracefully handle ERROR_PRIVILEGE_NOT_HELD).
static WinBenchResult bench_SetFileSecurityOwner() {
    const char* path = "data\\bench_owner.tmp";
    {
        HandleGuard h(CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    }

    auto res = runBench("SetNamedSecurityInfoA", "Permissions", 1000, 5, [&]() {
        PSECURITY_DESCRIPTOR pSD = nullptr;
        // ConvertStringSecurityDescriptorToSecurityDescriptorA (Win32/advapi32)
        if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
                "D:(A;;GA;;;CO)", SDDL_REVISION_1, &pSD, nullptr)) {
            PACL pDacl = nullptr;
            BOOL daclPresent = FALSE, daclDefault = FALSE;
            // GetSecurityDescriptorDacl – extract DACL pointer (Win32/advapi32)
            GetSecurityDescriptorDacl(pSD, &daclPresent, &pDacl, &daclDefault);
            // SetNamedSecurityInfoA – set security info on named object (Win32/advapi32)
            SetNamedSecurityInfoA(
                const_cast<char*>(path), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, pDacl, nullptr);
            LocalFree(pSD);
        }
    });
    DeleteFileA(path);
    return { res, "chown" };
}

// 15. GetProcessTimes  (≈ getrusage)
static WinBenchResult bench_GetProcessTimes() {
    auto res = runBench("GetProcessTimes", "Permissions", 1000, 5, [&]() {
        FILETIME fc{}, fe{}, fk{}, fu{};
        // GetProcessTimes – query process CPU time (Win32)
        GetProcessTimes(GetCurrentProcess(), &fc, &fe, &fk, &fu);
    });
    return { res, "getrusage" };
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // Worker --noop mode: used by CreateProcess benchmarks
    if (argc > 1 && std::string(argv[1]) == "--noop") {
        return 0;
    }

    Logger::instance().setLogFile("results/os_cca_win.log");
    logInfo("=== Win32 System Call Benchmark Suite ===");

    // Run the API demos first (Part A)
    demo_file_io();
    demo_proc();
    demo_mem();
    demo_ipc();
    demo_perm();

    // --demo-only: run Part A (functional demonstrations) and exit without benchmarking,
    // so results/windows_results.csv is not overwritten.
    if (argc > 1 && std::string(argv[1]) == "--demo-only") {
        logInfo("All Win32 system call demonstrations completed successfully.");
        return 0;
    }

    const std::string csvPath = "results/windows_results.csv";

    // Remove old CSV so header is written fresh
    std::filesystem::remove(csvPath);

    std::vector<WinBenchResult> results;

    std::cout << "Benchmarking Win32 APIs...\n";

    // ── File I/O ──────────────────────────────────────────────────────────────
    std::cout << "  [1/15] CreateFile..."; std::cout.flush();
    results.push_back(bench_CreateFile());         std::cout << " done\n";

    std::cout << "  [2/15] ReadFile...";   std::cout.flush();
    results.push_back(bench_ReadFile());           std::cout << " done\n";

    std::cout << "  [3/15] WriteFile...";  std::cout.flush();
    results.push_back(bench_WriteFile());          std::cout << " done\n";

    std::cout << "  [4/15] CloseHandle..."; std::cout.flush();
    results.push_back(bench_CloseHandle());        std::cout << " done\n";

    // ── Process ───────────────────────────────────────────────────────────────
    std::cout << "  [5/15] CreateProcess (200 iters × 5 runs)..."; std::cout.flush();
    results.push_back(bench_CreateProcess());      std::cout << " done\n";

    std::cout << "  [6/15] WaitForSingleObject (200 iters)..."; std::cout.flush();
    results.push_back(bench_WaitForSingleObject()); std::cout << " done\n";

    // ── Memory ────────────────────────────────────────────────────────────────
    std::cout << "  [7/15] VirtualAlloc..."; std::cout.flush();
    results.push_back(bench_VirtualAlloc());       std::cout << " done\n";

    std::cout << "  [8/15] VirtualFree..."; std::cout.flush();
    results.push_back(bench_VirtualFree());        std::cout << " done\n";

    // ── IPC ───────────────────────────────────────────────────────────────────
    std::cout << "  [9/15] CreatePipe..."; std::cout.flush();
    results.push_back(bench_CreatePipe());         std::cout << " done\n";

    std::cout << "  [10/15] CreateFileMapping..."; std::cout.flush();
    results.push_back(bench_CreateFileMapping());  std::cout << " done\n";

    std::cout << "  [11/15] MapViewOfFile..."; std::cout.flush();
    results.push_back(bench_MapViewOfFile());      std::cout << " done\n";

    std::cout << "  [12/15] UnmapViewOfFile..."; std::cout.flush();
    results.push_back(bench_UnmapViewOfFile());    std::cout << " done\n";

    // ── Permissions ───────────────────────────────────────────────────────────
    std::cout << "  [13/15] SetFileSecurity (DACL/chmod)..."; std::cout.flush();
    results.push_back(bench_SetFileSecurity());    std::cout << " done\n";

    std::cout << "  [14/15] SetNamedSecurityInfo (owner/chown)..."; std::cout.flush();
    results.push_back(bench_SetFileSecurityOwner()); std::cout << " done\n";

    std::cout << "  [15/15] GetProcessTimes (getrusage)..."; std::cout.flush();
    results.push_back(bench_GetProcessTimes());    std::cout << " done\n";

    // Write CSV
    for (const auto& r : results) {
        appendWinCsv(csvPath, r);
        logInfo("Benchmarked: " + r.base.syscall +
                " avg=" + std::to_string(r.base.avg_us) + " us");
    }

    std::cout << "\nResults written to: " << csvPath << "\n\n";

    // Print CSV to stdout
    std::ifstream csv(csvPath);
    if (csv.is_open()) {
        std::cout << "=== windows_results.csv ===\n";
        std::cout << csv.rdbuf();
    }

    return 0;
}
