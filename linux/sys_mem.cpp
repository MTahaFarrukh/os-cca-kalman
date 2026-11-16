/**
 * @file sys_mem.cpp
 * @brief Raw POSIX Memory mapping system calls: mmap, munmap
 * 
 * Part of CT-353 Operating Systems CCA.
 * Implements virtual memory page allocation and deallocation via raw kernel syscalls.
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include <sys/mman.h>
#include <unistd.h>

bool demo_mem() {
    logInfo("[DEMO] Starting Memory system calls demonstration (mmap, munmap)...");

    const size_t pageSize = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    const size_t allocSize = pageSize * 4; // 16 KB (4 pages)

    // 1. Raw syscall: mmap (anonymous private memory allocation)
    // syscall: mmap
    void* addr = ::mmap(nullptr, allocSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        logSysError("mmap");
        return false;
    }

    MmapRegion memRegion(addr, allocSize);

    // Touch and verify memory pages
    auto* intPtr = static_cast<int*>(memRegion.get());
    for (size_t i = 0; i < allocSize / sizeof(int); ++i) {
        intPtr[i] = static_cast<int>(i * 42);
    }

    // Verify written data
    bool verificationOk = true;
    for (size_t i = 0; i < 10; ++i) {
        if (intPtr[i] != static_cast<int>(i * 42)) {
            verificationOk = false;
            break;
        }
    }

    if (!verificationOk) {
        logError("[DEMO] Memory verification check failed.");
        return false;
    }

    // 2. Raw syscall: munmap (exercised via RAII reset)
    // syscall: munmap
    memRegion.reset();

    logInfo("[DEMO] Memory mapping (mmap/munmap) successfully demonstrated with " +
            std::to_string(allocSize) + " bytes.");
    return true;
}

#endif // _WIN32
