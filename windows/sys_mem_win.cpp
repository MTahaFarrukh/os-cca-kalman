/**
 * @file sys_mem_win.cpp
 * @brief Win32 virtual memory demo: VirtualAlloc, VirtualFree.
 *
 * Uses only Win32 API for memory management.
 * RAII cleanup via VirtualMemory (destructor calls VirtualFree).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif

#include "sys_win.hpp"
#include "../common/logger.hpp"

#include <string>
#include <cstring>

/** @brief Demonstrate Win32 virtual memory allocation, use, and release. */
void demo_mem() {
    logInfo("=== Win32 Virtual Memory Demo ===");

    const SIZE_T allocSize = 4 * 1024 * 1024; // 4 MiB

    // VirtualAlloc – reserve + commit anonymous pages (Win32)
    // MEM_COMMIT|MEM_RESERVE → immediately backed by pagefile
    // PAGE_READWRITE → read/write access
    void* rawPtr = VirtualAlloc(
        nullptr,                    // lpAddress  – OS chooses base
        allocSize,                  // dwSize     – bytes to allocate
        MEM_COMMIT | MEM_RESERVE,   // flAllocationType
        PAGE_READWRITE              // flProtect
    );

    if (!rawPtr) {
        logSysError("VirtualAlloc");
        return;
    }
    logInfo("VirtualAlloc: allocated " + std::to_string(allocSize / 1024) +
            " KiB at address 0x" +
            [&]{ char buf[32]; snprintf(buf, sizeof(buf), "%p", rawPtr); return std::string(buf); }());

    // Wrap in RAII guard – VirtualFree called in destructor
    VirtualMemory mem(rawPtr, allocSize);

    // Touch every page to confirm pages are committed and writable
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const SIZE_T pageSize = si.dwPageSize;
    char* p = static_cast<char*>(mem.get());
    SIZE_T touched = 0;
    for (SIZE_T offset = 0; offset < allocSize; offset += pageSize) {
        p[offset] = static_cast<char>(offset & 0xFF);  // write one byte per page
        ++touched;
    }
    logInfo("Page touch: wrote to " + std::to_string(touched) +
            " pages (page size = " + std::to_string(pageSize) + " B)");

    // Verify a few values
    bool ok = true;
    for (SIZE_T offset = 0; offset < allocSize && ok; offset += pageSize) {
        if (p[offset] != static_cast<char>(offset & 0xFF)) ok = false;
    }
    logInfo(std::string("Page verify: ") + (ok ? "PASS" : "FAIL"));

    // mem goes out of scope → VirtualFree(ptr, 0, MEM_RELEASE) automatically
    mem.free();  // VirtualFree – Win32 release (via RAII helper, explicit call)
    logInfo("VirtualFree: memory released");
    logInfo("Memory demo complete");
}
