/**
 * @file kalman_windows.cpp
 * @brief Win32 multi-process Kalman filter pipeline (Part C).
 *
 * Parent mode (default):
 *   1. Read data/sensors.bin with CreateFile/ReadFile into VirtualAlloc buffer.
 *   2. Create named pagefile-backed FileMapping for shared output array.
 *   3. Spawn N worker processes (CreateProcess) with inheritable pipe write ends.
 *   4. Workers filter their tracks and write status through the pipe.
 *   5. Parent WaitForSingleObject all workers.
 *   6. Write data/output_win.bin with CreateFile/WriteFile.
 *   7. Compute RMSE vs truth.bin and append to results/windows_kalman_scaling.csv.
 *
 * Worker mode (--worker <i> <N> <startTrack> <endTrack> <pipeHandle_hex>):
 *   1. Open named FileMapping.
 *   2. MapViewOfFile for write.
 *   3. Read sensors.bin with CreateFile/ReadFile.
 *   4. Run filterTrack on assigned tracks (q=25, dt=0.05, R=4).
 *   5. Write status report through inherited pipe HANDLE.
 *   6. Exit.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#ifdef ERROR
#undef ERROR
#endif

#include "../common/bench.hpp"
#include "../common/kalman.hpp"
#include "../common/logger.hpp"
#include "sys_win.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>

// ─────────────────────────────────────────────────────────────────────────────
// Constants matching sensor_gen.cpp
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int    NUM_TRACKS        = 8;
static constexpr int    SAMPLES_PER_TRACK = 100000;
static constexpr double DT                = 0.05;
static constexpr double Q                 = 25.0;
static constexpr double R_MEAS            = 4.0;
// Each worker repeats the Kalman pass this many times (identical output) so the compute
// phase is long enough to measure parallel speedup. Same value on Linux.
static constexpr int    COMPUTE_PASSES    = 50;

static const char* SHARED_MAP_NAME       = "Local\\CCA_KalmanOutput_Win";
static const char* READY_SEM_NAME        = "Local\\CCA_KalmanReady_Win";
static const char* GO_EVENT_NAME         = "Local\\CCA_KalmanGo_Win";
static const char* SENSOR_FILE           = "data\\sensors.bin";
static const char* TRUTH_FILE            = "data\\truth.bin";
static const char* OUTPUT_FILE           = "data\\output_win.bin";
static const char* SCALING_CSV           = "results\\windows_kalman_scaling.csv";

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::string exePath() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::string(buf);
}

static SIZE_T totalSamples() {
    return static_cast<SIZE_T>(NUM_TRACKS) * SAMPLES_PER_TRACK;
}

static SIZE_T outputBytes() {
    return sizeof(SensorFileHeader) + totalSamples() * sizeof(double);
}

// ─────────────────────────────────────────────────────────────────────────────
// Read all sensor measurements from sensors.bin via CreateFile + ReadFile
// ─────────────────────────────────────────────────────────────────────────────
static bool readSensors(const char* path,
                        SensorFileHeader& hdr,
                        VirtualMemory& mem) {
    // CreateFileA – open sensor binary for reading (Win32)
    HandleGuard hf(CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hf.valid()) {
        logSysError("CreateFileA(sensors.bin)");
        return false;
    }

    DWORD rd = 0;
    // ReadFile – read file header (Win32)
    if (!ReadFile(hf.get(), &hdr, sizeof(hdr), &rd, nullptr) || rd != sizeof(hdr)) {
        logSysError("ReadFile(sensors header)");
        return false;
    }

    SIZE_T payloadBytes = static_cast<SIZE_T>(hdr.num_tracks) *
                          hdr.samples_per_track * sizeof(double);

    // VirtualAlloc – reserve + commit buffer for all measurements (Win32)
    void* buf = VirtualAlloc(nullptr, payloadBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        logSysError("VirtualAlloc(sensors)");
        return false;
    }
    mem = VirtualMemory(buf, payloadBytes);

    // ReadFile – read all payload bytes (Win32)
    DWORD totalRead = 0;
    char* ptr = static_cast<char*>(mem.get());
    while (totalRead < static_cast<DWORD>(payloadBytes)) {
        DWORD chunk = 0;
        DWORD toRead = static_cast<DWORD>(
            std::min<SIZE_T>(payloadBytes - totalRead, 1024 * 1024));
        if (!ReadFile(hf.get(), ptr + totalRead, toRead, &chunk, nullptr) || chunk == 0)
            break;
        totalRead += chunk;
    }
    // CloseHandle via HandleGuard destructor
    return totalRead == static_cast<DWORD>(payloadBytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// WORKER MODE
// ─────────────────────────────────────────────────────────────────────────────
static int workerMain(int argc, char* argv[]) {
    // argv: --worker <i> <N> <startTrack> <endTrack> <pipeHandle_hex>
    if (argc < 7) {
        std::cerr << "Worker: bad args\n";
        return 1;
    }
    int    workerIdx  = std::atoi(argv[2]);
    int    startTrack = std::atoi(argv[4]);
    int    endTrack   = std::atoi(argv[5]);
    HANDLE hPipe      = reinterpret_cast<HANDLE>(
        static_cast<uintptr_t>(std::strtoull(argv[6], nullptr, 16)));

    // OpenFileMappingA – connect to parent's named shared memory (Win32)
    HandleGuard hMap(OpenFileMappingA(FILE_MAP_WRITE, FALSE, SHARED_MAP_NAME));
    if (!hMap.valid()) {
        logSysError("OpenFileMappingA(worker)");
        return 1;
    }

    // MapViewOfFile – map writable view of shared output (Win32)
    FileMappingView view(MapViewOfFile(hMap.get(), FILE_MAP_WRITE, 0, 0, 0));
    if (!view.valid()) {
        logSysError("MapViewOfFile(worker)");
        return 1;
    }

    // The shared region layout:
    //   [SensorFileHeader][NUM_TRACKS * SAMPLES_PER_TRACK doubles]
    char*   base   = static_cast<char*>(view.get());
    double* outBuf = reinterpret_cast<double*>(base + sizeof(SensorFileHeader));

    // Read sensor data from disk
    SensorFileHeader hdr{};
    VirtualMemory sensorMem;
    if (!readSensors(SENSOR_FILE, hdr, sensorMem)) return 1;

    const double* meas = static_cast<const double*>(sensorMem.get());

    // OpenSemaphoreA / OpenEventA – synchronise with parent so compute phase is timed
    // separately from process-creation latency (Win32)
    HandleGuard hReady(OpenSemaphoreA(SEMAPHORE_MODIFY_STATE, FALSE, READY_SEM_NAME));
    HandleGuard hGo(OpenEventA(SYNCHRONIZE, FALSE, GO_EVENT_NAME));
    if (!hReady.valid() || !hGo.valid()) {
        logSysError("OpenSemaphoreA/OpenEventA(worker)");
        return 1;
    }
    ReleaseSemaphore(hReady.get(), 1, nullptr);               // "I am ready"
    if (WaitForSingleObject(hGo.get(), 60000) != WAIT_OBJECT_0) {  // wait for "go"
        logSysError("WaitForSingleObject(go event)");
        return 1;
    }

    uint64_t t0 = nowNs();

    for (int pass = 0; pass < COMPUTE_PASSES; ++pass) {
        for (int track = startTrack; track < endTrack; ++track) {
            const double* trackMeas = meas + track * SAMPLES_PER_TRACK;
            double*       trackOut  = outBuf + track * SAMPLES_PER_TRACK;
            // filterTrack – apply Kalman filter (common/kalman.cpp)
            filterTrack(trackMeas, trackOut, SAMPLES_PER_TRACK,
                        DT, Q, R_MEAS);
        }
    }

    uint64_t elapsed_ms = (nowNs() - t0) / 1000000;

    // Report status back through inherited pipe write end
    // WriteFile – write worker status report through pipe (Win32)
    char report[128];
    int  rlen = snprintf(report, sizeof(report),
                         "WORKER %d: tracks [%d,%d) done in %llu ms\n",
                         workerIdx, startTrack, endTrack,
                         static_cast<unsigned long long>(elapsed_ms));
    DWORD wr = 0;
    WriteFile(hPipe, report, static_cast<DWORD>(rlen), &wr, nullptr);
    // CloseHandle for pipe write end – Win32
    CloseHandle(hPipe);

    std::cout << report;
    // UnmapViewOfFile via destructor, VirtualFree via destructor
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PARENT MODE – run with N workers
// ─────────────────────────────────────────────────────────────────────────────
struct ScalingResult {
    bool   ok{false};
    double total_ms{0};    // t0 (before first CreateProcess) -> all workers exited
    double spawn_ms{0};    // t0 -> all workers ready (process creation + init)
    double compute_ms{0};  // "go" signal -> all workers exited
    double rmse{0};
};

static ScalingResult runKalmanScaling(int N) {
    ScalingResult R;
    std::cout << "\n--- Running with N=" << N << " worker(s) ---\n";

    // ── 1. Read sensors.bin ─────────────────────────────────────────────────
    SensorFileHeader hdr{};
    VirtualMemory sensorMem;
    if (!readSensors(SENSOR_FILE, hdr, sensorMem)) return R;
    logInfo("Read sensors.bin: " + std::to_string(hdr.num_tracks) +
            " tracks × " + std::to_string(hdr.samples_per_track) + " samples");

    // ── 2. Create named shared output mapping ───────────────────────────────
    DWORD mapHigh = 0, mapLow = static_cast<DWORD>(outputBytes());
    // CreateFileMappingA – create pagefile-backed shared memory for output (Win32)
    HandleGuard hMap(CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE,
                                        mapHigh, mapLow,
                                        SHARED_MAP_NAME));
    if (!hMap.valid()) {
        logSysError("CreateFileMappingA(output)");
        return R;
    }

    // MapViewOfFile – map writable view in parent (Win32)
    FileMappingView parentView(MapViewOfFile(hMap.get(), FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!parentView.valid()) {
        logSysError("MapViewOfFile(parent)");
        return R;
    }

    // Write header into shared region
    std::memcpy(parentView.get(), &hdr, sizeof(hdr));

    // ── 3. Create pipes & spawn workers ─────────────────────────────────────
    // Distribute NUM_TRACKS tracks evenly among N workers
    std::vector<HandleGuard> pipeReads(N);
    std::vector<PROCESS_INFORMATION> pis(N);

    // Make pipe handles inheritable so child can use the write end
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    // CreateSemaphoreA / CreateEventA – ready counter + start barrier for workers (Win32)
    HandleGuard hReadySem(CreateSemaphoreA(nullptr, 0, N, READY_SEM_NAME));
    HandleGuard hGoEvent(CreateEventA(nullptr, TRUE, FALSE, GO_EVENT_NAME));
    if (!hReadySem.valid() || !hGoEvent.valid()) {
        logSysError("CreateSemaphoreA/CreateEventA");
        return R;
    }

    uint64_t t0 = nowNs();

    for (int i = 0; i < N; ++i) {
        int startTrack = (NUM_TRACKS * i) / N;
        int endTrack   = (NUM_TRACKS * (i + 1)) / N;

        HANDLE hR = nullptr, hW = nullptr;
        // CreatePipe – create pipe for worker status reporting (Win32)
        if (!CreatePipe(&hR, &hW, &sa, 0)) {
            logSysError("CreatePipe(worker pipe)");
            return R;
        }
        pipeReads[i] = HandleGuard(hR);

        // Make read end non-inheritable so child doesn't get a copy
        SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

        // Build command line:
        // <exe> --worker <i> <N> <start> <end> <pipeHandle_hex>
        char cmdBuf[MAX_PATH * 2];
        snprintf(cmdBuf, sizeof(cmdBuf),
                 "\"%s\" --worker %d %d %d %d %llx",
                 exePath().c_str(), i, N, startTrack, endTrack,
                 reinterpret_cast<unsigned long long>(hW));

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        // Don't redirect stdio – child uses inherited raw HANDLE for pipe only
        PROCESS_INFORMATION pi{};

        // CreateProcessA – spawn worker process (Win32)
        const uint64_t tcp0 = nowNs();
        if (!CreateProcessA(nullptr, cmdBuf, nullptr, nullptr,
                            TRUE,  // bInheritHandles = TRUE so pipe write end is inherited
                            0, nullptr, nullptr, &si, &pi)) {
            logSysError("CreateProcessA(worker)");
            CloseHandle(hW);
            return R;
        }
        pis[i] = pi;
        logInfo("CreateProcessA took " + std::to_string((nowNs() - tcp0) / 1000000.0) + " ms");

        // Close write end in parent (child owns it now)
        CloseHandle(hW);  // CloseHandle – Win32
        logInfo("Spawned worker " + std::to_string(i) +
                " for tracks [" + std::to_string(startTrack) + "," +
                std::to_string(endTrack) + ")");
    }

    // ── 4. Barrier: wait until every worker is initialised, then release them ─
    for (int i = 0; i < N; ++i) {
        // WaitForSingleObject – wait for worker "ready" semaphore token (Win32)
        if (WaitForSingleObject(hReadySem.get(), 30000) != WAIT_OBJECT_0) {
            logSysError("WaitForSingleObject(ready semaphore)");
            for (int k = 0; k < N; ++k) {
                if (pis[k].hProcess) TerminateProcess(pis[k].hProcess, 1);
            }
            return R;
        }
    }
    const uint64_t t_ready = nowNs();
    SetEvent(hGoEvent.get());          // SetEvent – start compute phase on all workers (Win32)
    const uint64_t t_go = nowNs();

    // ── 5a. Wait for all workers to exit ─────────────────────────────────────
    for (int i = 0; i < N; ++i) {
        // WaitForSingleObject – block until worker process exits (Win32)
        WaitForSingleObject(pis[i].hProcess, INFINITE);
    }
    const uint64_t t_done = nowNs();

    // ── 5b. Read worker status reports from pipes ────────────────────────────
    for (int i = 0; i < N; ++i) {
        CloseHandle(pis[i].hProcess);  // CloseHandle – Win32
        CloseHandle(pis[i].hThread);   // CloseHandle – Win32
        // ReadFile – read worker status report from pipe (Win32)
        char report[256] = {};
        DWORD rd = 0;
        ReadFile(pipeReads[i].get(), report, sizeof(report) - 1, &rd, nullptr);
        if (rd > 0) std::cout << "  " << std::string(report, rd);
    }

    R.spawn_ms   = static_cast<double>(t_ready - t0) / 1e6;
    R.compute_ms = static_cast<double>(t_done - t_go) / 1e6;
    R.total_ms   = static_cast<double>(t_done - t0) / 1e6;
    std::cout << std::fixed << std::setprecision(2)
              << "N=" << N << ": spawn+init " << R.spawn_ms
              << " ms | compute phase " << R.compute_ms
              << " ms | total " << R.total_ms << " ms\n";

    // ── 5. Write output_win.bin ──────────────────────────────────────────────
    const char* outPath = OUTPUT_FILE;
    // CreateFileA – create output file (Win32)
    HandleGuard hOut(CreateFileA(outPath, GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hOut.valid()) {
        logSysError("CreateFileA(output_win.bin)");
        return R;
    }

    DWORD wr = 0;
    // WriteFile – write filtered output to disk (Win32)
    if (!WriteFile(hOut.get(), parentView.get(), static_cast<DWORD>(outputBytes()), &wr, nullptr)) {
        logSysError("WriteFile(output_win.bin)");
        return R;
    }
    hOut.close();  // CloseHandle via RAII helper
    logInfo("Wrote output_win.bin (" + std::to_string(wr) + " bytes)");

    // ── 6. Compute RMSE vs truth.bin ─────────────────────────────────────────
    HandleGuard hTruth(CreateFileA(TRUTH_FILE, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!hTruth.valid()) {
        logSysError("CreateFileA(truth.bin)");
        return R;
    }

    SensorFileHeader truthHdr{};
    DWORD rdhdr = 0;
    ReadFile(hTruth.get(), &truthHdr, sizeof(truthHdr), &rdhdr, nullptr);

    SIZE_T truthBytes = static_cast<SIZE_T>(truthHdr.num_tracks) *
                        truthHdr.samples_per_track * sizeof(double);
    void* truthBuf = VirtualAlloc(nullptr, truthBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    VirtualMemory truthMem(truthBuf, truthBytes);

    DWORD totalRd = 0;
    char* tbp = static_cast<char*>(truthMem.get());
    while (totalRd < static_cast<DWORD>(truthBytes)) {
        DWORD chunk = 0;
        DWORD toRead = static_cast<DWORD>(
            std::min<SIZE_T>(truthBytes - totalRd, 1024 * 1024));
        ReadFile(hTruth.get(), tbp + totalRd, toRead, &chunk, nullptr);
        if (chunk == 0) break;
        totalRd += chunk;
    }
    hTruth.close();

    const double* truthData = static_cast<const double*>(truthMem.get());
    const double* outData   = reinterpret_cast<const double*>(
        static_cast<const char*>(parentView.get()) + sizeof(SensorFileHeader));

    double sse = 0.0;
    size_t cnt = 0;
    for (size_t s = 0; s < totalSamples(); ++s) {
        double diff = outData[s] - truthData[s];
        sse += diff * diff;
        ++cnt;
    }
    double rmse = std::sqrt(sse / static_cast<double>(cnt));
    std::cout << "RMSE vs truth.bin: " << std::fixed << std::setprecision(4)
              << rmse << " m\n";
    R.rmse = rmse;

    // ── 7. Cleanup shared mapping ────────────────────────────────────────────
    // parentView and hMap destructors call UnmapViewOfFile + CloseHandle
    R.ok = true;
    return R;
}

// ─────────────────────────────────────────────────────────────────────────────
// Write scaling CSV row
// ─────────────────────────────────────────────────────────────────────────────
static void appendScalingCsv(const std::string& path, int N,
                              const ScalingResult& r,
                              double speedupTotal, double speedupCompute) {
    namespace fs = std::filesystem;
    std::error_code ec;
    bool is_empty = !fs::exists(path, ec) || fs::file_size(path, ec) == 0;

    std::ofstream out(path, std::ios::app);
    if (is_empty)
        out << "N,total_ms,spawn_ms,compute_ms,speedup_total,speedup_compute,rmse_m\n";
    out << std::fixed << std::setprecision(4);
    out << N << "," << r.total_ms << "," << r.spawn_ms << "," << r.compute_ms << ","
        << speedupTotal << "," << speedupCompute << "," << r.rmse << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // ── Worker mode ──────────────────────────────────────────────────────────
    if (argc > 1 && std::string(argv[1]) == "--worker") {
        return workerMain(argc, argv);
    }

    // ── Noop mode (used by syscall_bench_win for CreateProcess benchmark) ────
    if (argc > 1 && std::string(argv[1]) == "--noop") {
        return 0;
    }

    Logger::instance().setLogFile("results/os_cca_win_kalman.log");
    logInfo("=== kalman_win: Multi-Process Kalman Pipeline ===");

    std::filesystem::create_directories("data");
    std::filesystem::create_directories("results");

    // Check sensor data exists
    if (!std::filesystem::exists(SENSOR_FILE)) {
        std::cerr << "ERROR: " << SENSOR_FILE
                  << " not found. Run sensor_gen first.\n";
        return 1;
    }

    // Run scaling study: N = 1, 2, 4, 8
    std::filesystem::remove(SCALING_CSV);
    std::vector<int> Ns = {1, 2, 4, 8};
    double base_total = -1.0, base_compute = -1.0;

    for (int N : Ns) {
        ScalingResult r = runKalmanScaling(N);
        if (!r.ok) {
            std::cerr << "Failed for N=" << N << "\n";
            continue;
        }
        if (base_total < 0)   base_total   = r.total_ms;
        if (base_compute < 0) base_compute = r.compute_ms;
        const double spTotal   = (r.total_ms   > 0) ? base_total   / r.total_ms   : 1.0;
        const double spCompute = (r.compute_ms > 0) ? base_compute / r.compute_ms : 1.0;
        appendScalingCsv(SCALING_CSV, N, r, spTotal, spCompute);
    }

    if (base_total < 0) {
        logError("All Kalman runs failed. Run sensor_gen.exe first and start the program from the project root "
                 "(it needs data\\sensors.bin and data\\truth.bin).");
        return 1;
    }

    std::cout << "\n=== windows_kalman_scaling.csv ===\n";
    std::ifstream csv(SCALING_CSV);
    if (csv.is_open()) std::cout << csv.rdbuf();

    return 0;
}
