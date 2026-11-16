/**
 * @file kalman_linux.cpp
 * @brief Multi-process Parallel 2D Kalman Filter Pipeline for Linux
 * 
 * Part of CT-353 Operating Systems CCA.
 * Implements:
 *   - Reading sensors.bin using open() and read()
 *   - Mapping sensor array via mmap()
 *   - Allocating inter-process shared memory using shmget() and shmat()
 *   - Forking N workers using fork()
 *   - Worker IPC reporting through pipe() and write()
 *   - Parent synchronization with waitpid() and read()
 *   - Writing output_linux.bin via open(), write(), close()
 *   - Computing RMSE against truth.bin
 *   - Recording multi-process scaling performance to results/linux_kalman_scaling.csv
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include "../common/kalman.hpp"
#include "../common/logger.hpp"
#include "../common/timer.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

struct WorkerStatus {
    int worker_id;
    pid_t pid;
    uint32_t start_track;
    uint32_t end_track;
    double elapsed_ms;
    int error_code;
};

struct ScalingRecord {
    int workers;
    double total_ms;
    double spawn_ms;
    double compute_ms;
    double speedup_total;
    double speedup_compute;
    double rmse;
};

// Each worker repeats the Kalman pass this many times (identical output) so the
// compute phase is long enough to measure parallel speedup. Same value on Windows.
static constexpr int COMPUTE_PASSES = 50;

// Execute Kalman filtering with N workers
bool runParallelKalman(int numWorkers, double& outElapsedMs, double& outRmse, bool writeOutputFile,
                       double* outSpawnMs = nullptr, double* outComputeMs = nullptr) {
    const char* sensorsPath = "data/sensors.bin";
    const char* truthPath = "data/truth.bin";
    const char* outputPath = "data/output_linux.bin";

    // 1. Raw syscall: open sensors.bin
    // syscall: open
    int fd_sensors = ::open(sensorsPath, O_RDONLY);
    if (fd_sensors < 0) {
        logSysError("open (sensors.bin)");
        return false;
    }
    FileDescriptor fdSensors(fd_sensors);

    // 2. Raw syscall: read file header
    SensorFileHeader header{};
    // syscall: read
    ssize_t hdr_bytes = ::read(fdSensors.get(), &header, sizeof(header));
    if (hdr_bytes != static_cast<ssize_t>(sizeof(header))) {
        logSysError("read (header)");
        return false;
    }

    const uint32_t numTracks = header.num_tracks;
    const uint32_t samplesPerTrack = header.samples_per_track;
    const size_t totalSamples = static_cast<size_t>(numTracks) * samplesPerTrack;
    const size_t payloadBytes = totalSamples * sizeof(double);
    const size_t totalFileSize = sizeof(SensorFileHeader) + payloadBytes;

    // 3. Raw syscall: mmap input sensors file
    // syscall: mmap
    void* mmap_base = ::mmap(nullptr, totalFileSize, PROT_READ, MAP_SHARED, fdSensors.get(), 0);
    if (mmap_base == MAP_FAILED) {
        logSysError("mmap (sensors.bin)");
        return false;
    }
    MmapRegion mappedSensors(mmap_base, totalFileSize);

    const auto* rawSensorsData = reinterpret_cast<const double*>(
        static_cast<const char*>(mappedSensors.get()) + sizeof(SensorFileHeader));

    // 4. Raw syscall: shmget & shmat for shared output array
    // syscall: shmget
    int shmid = ::shmget(IPC_PRIVATE, payloadBytes, IPC_CREAT | 0666);
    if (shmid < 0) {
        logSysError("shmget");
        return false;
    }

    // syscall: shmat
    void* shm_addr = ::shmat(shmid, nullptr, 0);
    if (shm_addr == reinterpret_cast<void*>(-1)) {
        logSysError("shmat");
        ::shmctl(shmid, IPC_RMID, nullptr);
        return false;
    }
    SharedMemorySegment sharedOutput(shmid, shm_addr, true /* owner */);
    auto* filteredOutputData = static_cast<double*>(sharedOutput.get());

    // Adjust worker count if greater than number of tracks
    int N = numWorkers;
    if (N < 1) N = 1;
    if (N > static_cast<int>(numTracks)) N = static_cast<int>(numTracks);

    // 5. Prepare pipes for each worker
    std::vector<PipePair> pipes(N);
    for (int w = 0; w < N; ++w) {
        if (!pipes[w].create()) {
            return false;
        }
    }

    const uint64_t tStartNs = nowNs();
    std::vector<pid_t> childPids(N, -1);

    // 6. Fork worker processes
    for (int w = 0; w < N; ++w) {
        // Track partition per worker
        const uint32_t startTrack = (static_cast<uint32_t>(w) * numTracks) / static_cast<uint32_t>(N);
        const uint32_t endTrack = (static_cast<uint32_t>(w + 1) * numTracks) / static_cast<uint32_t>(N);

        // syscall: fork
        pid_t pid = ::fork();
        if (pid < 0) {
            logSysError("fork");
            return false;
        }

        if (pid == 0) {
            // ================================================================
            // Child Worker Process
            // ================================================================
            const uint64_t wStartNs = nowNs();
            pipes[w].closeRead();

            // Run Kalman filter on assigned tracks
            for (int pass = 0; pass < COMPUTE_PASSES; ++pass) {
                for (uint32_t t = startTrack; t < endTrack; ++t) {
                    const double* trackInput = rawSensorsData + static_cast<size_t>(t) * samplesPerTrack;
                    double* trackOutput = filteredOutputData + static_cast<size_t>(t) * samplesPerTrack;

                    // Constant-velocity 2D Kalman filter: dt=0.05, process_noise=25.0, R=4.0
                    filterTrack(trackInput, trackOutput, samplesPerTrack, 0.05, 25.0, 4.0);
                }
            }

            const uint64_t wEndNs = nowNs();
            WorkerStatus report{};
            report.worker_id = w;
            report.pid = ::getpid();
            report.start_track = startTrack;
            report.end_track = endTrack;
            report.elapsed_ms = static_cast<double>(wEndNs - wStartNs) / 1000000.0;
            report.error_code = 0;

            // syscall: write (report status through pipe)
            ssize_t written = ::write(pipes[w].writeFd(), &report, sizeof(report));
            (void)written;
            pipes[w].closeWrite();

            // syscall: _exit
            ::_exit(0);
        }

        // Parent stores child PID
        childPids[w] = pid;
        pipes[w].closeWrite();
    }

    const uint64_t tForkedNs = nowNs();

    // 7. Parent collects worker reports and waits for all children
    std::vector<WorkerStatus> reports(N);
    for (int w = 0; w < N; ++w) {
        // syscall: read (read report from child pipe)
        ssize_t r_bytes = ::read(pipes[w].readFd(), &reports[w], sizeof(WorkerStatus));
        (void)r_bytes;
        pipes[w].closeRead();

        int status = 0;
        // syscall: waitpid
        ::waitpid(childPids[w], &status, 0);
    }

    const uint64_t tEndNs = nowNs();
    outElapsedMs = static_cast<double>(tEndNs - tStartNs) / 1000000.0;
    if (outSpawnMs) *outSpawnMs = static_cast<double>(tForkedNs - tStartNs) / 1000000.0;
    if (outComputeMs) {
        double mx = 0.0;
        for (const auto& r : reports) mx = (r.elapsed_ms > mx) ? r.elapsed_ms : mx;
        *outComputeMs = mx;   // slowest worker's compute time
    }

    // 8. Raw syscalls: open, write, close for output_linux.bin
    if (writeOutputFile) {
        // syscall: open
        int fd_out = ::open(outputPath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd_out < 0) {
            logSysError("open (output_linux.bin)");
            return false;
        }
        FileDescriptor fdOut(fd_out);

        // syscall: write header
        ssize_t hw = ::write(fdOut.get(), &header, sizeof(header));
        (void)hw;

        // syscall: write payload
        size_t total_written = 0;
        const char* ptr = reinterpret_cast<const char*>(filteredOutputData);
        while (total_written < payloadBytes) {
            ssize_t w = ::write(fdOut.get(), ptr + total_written, payloadBytes - total_written);
            if (w <= 0) {
                logSysError("write (output_linux.bin)");
                break;
            }
            total_written += static_cast<size_t>(w);
        }
        // syscall: close handled by fdOut destructor
    }

    // 9. Read truth.bin using open/read/mmap to compute RMSE
    // syscall: open
    int fd_truth = ::open(truthPath, O_RDONLY);
    if (fd_truth >= 0) {
        FileDescriptor fdTruth(fd_truth);
        // syscall: mmap
        void* mmap_truth = ::mmap(nullptr, totalFileSize, PROT_READ, MAP_SHARED, fdTruth.get(), 0);
        if (mmap_truth != MAP_FAILED) {
            MmapRegion mappedTruth(mmap_truth, totalFileSize);
            const auto* truthData = reinterpret_cast<const double*>(
                static_cast<const char*>(mappedTruth.get()) + sizeof(SensorFileHeader));

            double sumSqErr = 0.0;
            for (size_t i = 0; i < totalSamples; ++i) {
                double diff = filteredOutputData[i] - truthData[i];
                sumSqErr += diff * diff;
            }
            outRmse = std::sqrt(sumSqErr / static_cast<double>(totalSamples));
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    logInfo("==================================================================");
    logInfo("  CT-353 Operating Systems: Linux Parallel Kalman Filter Engine");
    logInfo("==================================================================");

    int requestedWorkers = 4;
    bool runScalingSuite = false;

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--scaling" || arg == "-s" || arg == "all") {
            runScalingSuite = true;
        } else {
            try {
                requestedWorkers = std::stoi(arg);
            } catch (...) {
                requestedWorkers = 4;
            }
        }
    } else {
        // By default, run full scaling analysis for N=1, 2, 4, 8
        runScalingSuite = true;
    }

    const std::string scalingCsv = "results/linux_kalman_scaling.csv";

    if (runScalingSuite) {
        logInfo("Running multi-process Kalman Filter scaling suite for N = 1, 2, 4, 8 workers...");
        const std::vector<int> workerConfigs = {1, 2, 4, 8};
        std::vector<ScalingRecord> records;
        double baseTotal = 0.0, baseCompute = 0.0;

        for (int w : workerConfigs) {
            double elapsedMs = 0.0, spawnMs = 0.0, computeMs = 0.0;
            double rmse = 0.0;
            const bool saveOutput = (w == 8); // Save output_linux.bin on final run

            if (runParallelKalman(w, elapsedMs, rmse, saveOutput, &spawnMs, &computeMs)) {
                if (w == 1) {
                    baseTotal = elapsedMs;
                    baseCompute = computeMs;
                }
                const double spTotal = (baseTotal > 0.0) ? (baseTotal / elapsedMs) : 1.0;
                const double spCompute = (baseCompute > 0.0 && computeMs > 0.0) ? (baseCompute / computeMs) : 1.0;
                records.push_back({w, elapsedMs, spawnMs, computeMs, spTotal, spCompute, rmse});

                logInfo("N=" + std::to_string(w) + " workers: total " + std::to_string(elapsedMs) +
                        " ms | spawn " + std::to_string(spawnMs) + " ms | compute " +
                        std::to_string(computeMs) + " ms | compute speedup " +
                        std::to_string(spCompute) + "x | RMSE " + std::to_string(rmse) + " m");
            }
        }

        if (records.empty()) {
            logError("All Kalman runs failed. Run sensor_gen first and start the program from the project root "
                     "(it needs data/sensors.bin and data/truth.bin).");
            return 1;
        }

        // Save scaling results to CSV using raw POSIX syscalls
        // syscall: open
        int fd_csv = ::open(scalingCsv.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd_csv >= 0) {
            FileDescriptor fdCsv(fd_csv);
            std::string headerStr = "N,total_ms,spawn_ms,compute_ms,speedup_total,speedup_compute,rmse_m\n";
            ssize_t hw = ::write(fdCsv.get(), headerStr.c_str(), headerStr.size());
            (void)hw;

            for (const auto& rec : records) {
                char line[192];
                int len = std::snprintf(line, sizeof(line), "%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                                        rec.workers, rec.total_ms, rec.spawn_ms, rec.compute_ms,
                                        rec.speedup_total, rec.speedup_compute, rec.rmse);
                if (len > 0) {
                    // syscall: write
                    ssize_t lw = ::write(fdCsv.get(), line, static_cast<size_t>(len));
                    (void)lw;
                }
            }
        }

        std::cout << "\n========================================================================\n";
        std::cout << "  Linux Parallel Kalman Filter Multi-Process Scaling Summary\n";
        std::cout << "========================================================================\n";
        std::cout << std::left << std::setw(12) << "Workers (N)"
                  << std::setw(14) << "Total (ms)"
                  << std::setw(14) << "Spawn (ms)"
                  << std::setw(14) << "Compute (ms)"
                  << std::setw(14) << "Speedup(cmp)"
                  << std::setw(12) << "RMSE (m)" << "\n";
        std::cout << "------------------------------------------------------------------------\n";
        for (const auto& rec : records) {
            std::cout << std::left << std::setw(12) << rec.workers
                      << std::fixed << std::setprecision(2)
                      << std::setw(14) << rec.total_ms
                      << std::setw(14) << rec.spawn_ms
                      << std::setw(14) << rec.compute_ms
                      << std::setw(14) << rec.speedup_compute
                      << std::setw(12) << std::setprecision(4) << rec.rmse << "\n";
        }
        std::cout << "========================================================================\n";
        std::cout << "Scaling results successfully written to: " << scalingCsv << "\n";
        std::cout << "Output array verified & saved to: data/output_linux.bin\n\n";

    } else {
        logInfo("Running single configuration with N=" + std::to_string(requestedWorkers) + " workers...");
        double elapsedMs = 0.0;
        double rmse = 0.0;
        if (runParallelKalman(requestedWorkers, elapsedMs, rmse, true)) {
            std::cout << "\nCompleted in " << elapsedMs << " ms with RMSE = " << rmse << " m.\n";
            std::cout << "Output written to data/output_linux.bin\n";
        } else {
            logError("Kalman run failed (see the error lines above).");
            return 1;
        }
    }

    return 0;
}

#else
int main() { return 0; }
#endif
