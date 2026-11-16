/**
 * @file sys_file_win.cpp
 * @brief Win32 file I/O demo: CreateFile, ReadFile, WriteFile, CloseHandle.
 *
 * Uses only Win32 API for all I/O operations (no fstream / stdio).
 * RAII cleanup via HandleGuard (destructor calls CloseHandle).
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

#include <cstdio>
#include <cstring>
#include <string>

/** @brief Demonstrate Win32 file creation, writing, reading, and deletion. */
void demo_file_io() {
    logInfo("=== Win32 File I/O Demo ===");

    const char* path    = "data\\win_demo_file.tmp";
    const char  payload[] = "Hello from Win32 File I/O!\n";
    const DWORD payloadLen = static_cast<DWORD>(sizeof(payload) - 1);

    // ── CREATE / WRITE ──────────────────────────────────────────────────────
    // CreateFileA – create or truncate a file for writing (Win32)
    HandleGuard hWrite(
        CreateFileA(
            path,                        // lpFileName
            GENERIC_WRITE,               // dwDesiredAccess
            0,                           // dwShareMode  (exclusive)
            nullptr,                     // lpSecurityAttributes
            CREATE_ALWAYS,               // dwCreationDisposition – create/truncate
            FILE_ATTRIBUTE_NORMAL,       // dwFlagsAndAttributes
            nullptr                      // hTemplateFile
        )
    );

    if (!hWrite.valid()) {
        logSysError("CreateFileA(WRITE)");
        return;
    }
    logInfo("CreateFileA: file created for writing → " + std::string(path));

    // WriteFile – write buffer to file (Win32)
    DWORD written = 0;
    if (!WriteFile(hWrite.get(), payload, payloadLen, &written, nullptr)) {
        logSysError("WriteFile");
        return;
    }
    logInfo("WriteFile: wrote " + std::to_string(written) + " bytes");

    // HandleGuard destructor calls CloseHandle on hWrite here when hWrite goes
    // out of scope; we close it explicitly now so we can re-open for reading.
    hWrite.close();  // CloseHandle – Win32 file close (via RAII helper)
    logInfo("CloseHandle: write handle released");

    // ── READ ────────────────────────────────────────────────────────────────
    // CreateFileA – open existing file for reading (Win32)
    HandleGuard hRead(
        CreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,               // must already exist
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        )
    );

    if (!hRead.valid()) {
        logSysError("CreateFileA(READ)");
        return;
    }

    char buf[256] = {};
    DWORD bytesRead = 0;
    // ReadFile – read bytes from file (Win32)
    if (!ReadFile(hRead.get(), buf, sizeof(buf) - 1, &bytesRead, nullptr)) {
        logSysError("ReadFile");
        return;
    }
    logInfo("ReadFile: read " + std::to_string(bytesRead) +
            " bytes → \"" + std::string(buf, bytesRead > 40 ? 40 : bytesRead) + "\"");

    // hRead.~HandleGuard() → CloseHandle automatically
    hRead.close();  // CloseHandle – Win32 file close (via RAII helper)
    logInfo("CloseHandle: read handle released");

    // Cleanup temp file
    DeleteFileA(path);
    logInfo("File I/O demo complete");
}
