#ifndef OS_CCA_BENCH_HPP
#define OS_CCA_BENCH_HPP

#include "timer.hpp"

#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <type_traits>
#include <algorithm>

/**
 * @brief Platform-agnostic container for resource usage metrics.
 * Populated by platform-specific functions declared below.
 */
struct ResourceUsage {
    double cpu_user_us{0.0}; // User-mode CPU time in microseconds
    double cpu_sys_us{0.0};  // Kernel / System-mode CPU time in microseconds
    int64_t maxrss_kb{0};    // Peak resident set size (working set) in Kilobytes
};

/**
 * @brief Platform-specific resource usage query.
 * Declared here, defined in linux/ (sys_proc.cpp) and windows/ (sys_proc_win.cpp).
 */
ResourceUsage getPlatformResourceUsage();

/**
 * @brief Results structure for benchmark runs.
 * Columns: syscall,category,iterations,avg_us,min_us,max_us,cpu_user_us,cpu_sys_us,maxrss_kb
 */
struct BenchmarkResult {
    std::string syscall;
    std::string category;
    size_t iterations{0};
    double avg_us{0.0};
    double min_us{0.0};
    double max_us{0.0};
    double cpu_user_us{0.0};
    double cpu_sys_us{0.0};
    int64_t maxrss_kb{0};
};

/**
 * @brief Template helper to run a benchmark with multiple repetitions.
 * 
 * Measures wall-clock time per iteration in microseconds (avg, min, max)
 * across runs, along with total CPU user/system time and peak RSS.
 * 
 * @tparam Func Callable invoked for each iteration (accepts void or size_t index).
 * @param name Name of the syscall or operation being benchmarked.
 * @param category Category (e.g., "File I/O", "Process", "Memory", "IPC", "Permissions").
 * @param iterations Number of invocations per benchmark run.
 * @param runs Number of independent benchmark runs to compute statistics.
 * @param lambda The workload callable.
 * @return BenchmarkResult Aggregated benchmark metrics.
 */
template <typename Func>
BenchmarkResult runBench(const std::string& name,
                         const std::string& category,
                         size_t iterations,
                         size_t runs,
                         Func&& lambda)
{
    const size_t total_runs = (runs == 0) ? 1 : runs;
    const size_t total_iters = (iterations == 0) ? 1 : iterations;

    std::vector<double> run_times_us;
    run_times_us.reserve(total_runs);

    // Capture baseline resource usage
    const ResourceUsage res_before = getPlatformResourceUsage();

    for (size_t r = 0; r < total_runs; ++r) {
        const uint64_t start_ns = nowNs();

        for (size_t i = 0; i < total_iters; ++i) {
            if constexpr (std::is_invocable_v<Func, size_t>) {
                lambda(i);
            } else {
                lambda();
            }
        }

        const uint64_t end_ns = nowNs();
        const double total_elapsed_us = static_cast<double>(end_ns - start_ns) / 1000.0;
        const double per_iter_us = total_elapsed_us / static_cast<double>(total_iters);
        run_times_us.push_back(per_iter_us);
    }

    // Capture final resource usage
    const ResourceUsage res_after = getPlatformResourceUsage();

    double min_us = run_times_us.front();
    double max_us = run_times_us.front();
    double sum_us = 0.0;

    for (double t : run_times_us) {
        if (t < min_us) min_us = t;
        if (t > max_us) max_us = t;
        sum_us += t;
    }
    const double avg_us = sum_us / static_cast<double>(total_runs);

    BenchmarkResult result;
    result.syscall = name;
    result.category = category;
    result.iterations = total_iters;
    result.avg_us = avg_us;
    result.min_us = min_us;
    result.max_us = max_us;

    result.cpu_user_us = (res_after.cpu_user_us >= res_before.cpu_user_us)
                         ? (res_after.cpu_user_us - res_before.cpu_user_us) : 0.0;
    result.cpu_sys_us = (res_after.cpu_sys_us >= res_before.cpu_sys_us)
                        ? (res_after.cpu_sys_us - res_before.cpu_sys_us) : 0.0;
    result.maxrss_kb = res_after.maxrss_kb;

    return result;
}

/**
 * @brief Append a benchmark result row to a CSV file.
 * Automatically writes the CSV header if the file does not exist or is empty.
 * Header format: syscall,category,iterations,avg_us,min_us,max_us,cpu_user_us,cpu_sys_us,maxrss_kb
 * 
 * @param csvPath Path to the CSV file.
 * @param res BenchmarkResult to append.
 * @return true if row written successfully, false otherwise.
 */
inline bool appendBenchmarkCsv(const std::string& csvPath, const BenchmarkResult& res) {
    std::error_code ec;
    const std::filesystem::path p(csvPath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    const bool file_exists = std::filesystem::exists(p, ec);
    const bool is_empty = !file_exists || (std::filesystem::file_size(p, ec) == 0);

    std::ofstream out(csvPath, std::ios::out | std::ios::app);
    if (!out.is_open()) {
        return false;
    }

    if (is_empty) {
        out << "syscall,category,iterations,avg_us,min_us,max_us,cpu_user_us,cpu_sys_us,maxrss_kb\n";
    }

    out << std::fixed << std::setprecision(4);
    out << res.syscall << ","
        << res.category << ","
        << res.iterations << ","
        << res.avg_us << ","
        << res.min_us << ","
        << res.max_us << ","
        << res.cpu_user_us << ","
        << res.cpu_sys_us << ","
        << res.maxrss_kb << "\n";

    out.flush();
    return true;
}

#endif // OS_CCA_BENCH_HPP
