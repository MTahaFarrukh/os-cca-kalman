/**
 * @file sys_ipc_win.cpp
 * @brief Win32 IPC demo: CreatePipe, CreateFileMapping, MapViewOfFile.
 *
 * Uses only Win32 API for inter-process communication primitives.
 * RAII cleanup via HandleGuard / FileMappingView.
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

/** @brief Demonstrate Win32 anonymous pipe I/O. */
static void demo_pipe() {
    logInfo("--- Anonymous Pipe (CreatePipe / ReadFile / WriteFile) ---");

    // CreatePipe – create anonymous byte-stream pipe (Win32)
    HANDLE hR = nullptr, hW = nullptr;
    if (!CreatePipe(&hR, &hW, nullptr, 0)) {
        logSysError("CreatePipe");
        return;
    }
    HandleGuard pipeRead(hR);   // CloseHandle on destructor
    HandleGuard pipeWrite(hW);

    logInfo("CreatePipe: read=" + [&]{ char b[32]; snprintf(b,sizeof(b),"%p",hR); return std::string(b); }()
           + " write=" + [&]{ char b[32]; snprintf(b,sizeof(b),"%p",hW); return std::string(b); }());

    const char msg[] = "Win32 pipe message";
    DWORD written = 0;
    // WriteFile – write to pipe write end (Win32)
    if (!WriteFile(pipeWrite.get(), msg, static_cast<DWORD>(sizeof(msg)), &written, nullptr)) {
        logSysError("WriteFile(pipe)");
        return;
    }
    logInfo("WriteFile(pipe): wrote " + std::to_string(written) + " bytes");

    char buf[128] = {};
    DWORD rd = 0;
    // ReadFile – read from pipe read end (Win32)
    if (!ReadFile(pipeRead.get(), buf, sizeof(buf) - 1, &rd, nullptr)) {
        logSysError("ReadFile(pipe)");
        return;
    }
    logInfo("ReadFile(pipe): received \"" + std::string(buf, rd) + "\"");
    // HandleGuard destructors call CloseHandle on both pipe ends
}

/** @brief Demonstrate Win32 named shared memory (pagefile-backed). */
static void demo_shared_mem() {
    logInfo("--- Named Shared Memory (CreateFileMapping / MapViewOfFile) ---");

    const char* mapName = "Local\\CCA_DemoShm";
    const DWORD mapSize = 4096;

    // CreateFileMappingA – create named, pagefile-backed memory object (Win32)
    // INVALID_HANDLE_VALUE as file handle → backed by system pagefile (anonymous)
    HandleGuard hMap(
        CreateFileMappingA(
            INVALID_HANDLE_VALUE,   // hFile – pagefile-backed
            nullptr,                // lpFileMappingAttributes
            PAGE_READWRITE,         // flProtect
            0,                      // dwMaximumSizeHigh
            mapSize,                // dwMaximumSizeLow
            mapName                 // lpName
        )
    );

    if (!hMap.valid()) {
        logSysError("CreateFileMappingA");
        return;
    }
    logInfo("CreateFileMappingA: created mapping \"" + std::string(mapName) + "\"");

    // MapViewOfFile – map a view of the file mapping into address space (Win32)
    FileMappingView view(
        MapViewOfFile(
            hMap.get(),             // hFileMappingObject
            FILE_MAP_ALL_ACCESS,    // dwDesiredAccess
            0,                      // dwFileOffsetHigh
            0,                      // dwFileOffsetLow
            0                       // dwNumberOfBytesToMap – map entire object
        )
    );

    if (!view.valid()) {
        logSysError("MapViewOfFile");
        return;
    }
    logInfo("MapViewOfFile: mapped view at 0x" +
            [&]{ char b[32]; snprintf(b,sizeof(b),"%p",view.get()); return std::string(b); }());

    // Write a sentinel value into shared memory
    const char greeting[] = "SharedMem-Win32";
    std::memcpy(view.get(), greeting, sizeof(greeting));
    logInfo("Shared mem write: \"" + std::string(view.as<char>()) + "\"");

    // UnmapViewOfFile – remove the mapping from address space (Win32)
    view.unmap();   // via RAII helper (calls UnmapViewOfFile)
    logInfo("UnmapViewOfFile: view unmapped");

    // hMap destructor calls CloseHandle – Win32 cleanup
    hMap.close();
    logInfo("CloseHandle: file mapping object released");
}

/** @brief Demo entry point for IPC primitives. */
void demo_ipc() {
    logInfo("=== Win32 IPC Demo ===");
    demo_pipe();
    demo_shared_mem();
    logInfo("IPC demo complete");
}
