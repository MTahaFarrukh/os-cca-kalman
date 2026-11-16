#ifndef OS_CCA_TIMER_HPP
#define OS_CCA_TIMER_HPP

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/**
 * @brief High-resolution monotonic timestamp in nanoseconds on Windows.
 * Uses QueryPerformanceCounter and QueryPerformanceFrequency.
 */
inline uint64_t nowNs() {
    static const uint64_t freq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();

    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    const uint64_t c = static_cast<uint64_t>(count.QuadPart);

    // Prevent integer overflow when multiplying by 1e9:
    // c * 1,000,000,000 can exceed 2^64-1 after long system uptime.
    // Partitioning into seconds and fractional remainder prevents overflow:
    const uint64_t sec = c / freq;
    const uint64_t rem = c % freq;
    return sec * 1000000000ULL + (rem * 1000000000ULL) / freq;
}

#else
#include <time.h>

/**
 * @brief High-resolution monotonic timestamp in nanoseconds on Linux/POSIX.
 * Uses clock_gettime with CLOCK_MONOTONIC.
 */
inline uint64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

#endif // _WIN32

/**
 * @brief Convenience conversion to microseconds.
 */
inline double nowUs() {
    return static_cast<double>(nowNs()) / 1000.0;
}

/**
 * @brief Convenience conversion to milliseconds.
 */
inline double nowMs() {
    return static_cast<double>(nowNs()) / 1000000.0;
}

#endif // OS_CCA_TIMER_HPP
