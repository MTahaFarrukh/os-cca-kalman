/**
 * @file sys_perm_win.cpp
 * @brief Win32 file security demo: SetFileSecurity with a DACL built via
 *        ConvertStringSecurityDescriptorToSecurityDescriptor (SDDL).
 *
 * Uses only Win32 / advapi32 API. Full error handling via logSysError.
 * Handles ERROR_PRIVILEGE_NOT_HELD gracefully (logs and continues).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#ifdef ERROR
#undef ERROR
#endif

#include "sys_win.hpp"
#include "../common/logger.hpp"

#include <string>

/** @brief Demonstrate SetFileSecurity with a DACL from an SDDL string. */
void demo_perm() {
    logInfo("=== Win32 File Security (SetFileSecurity) Demo ===");

    // Create a temporary file to apply security settings to
    const char* tempPath = "data\\win_perm_test.tmp";

    // CreateFileA – create temp file (Win32)
    HandleGuard hTmp(
        CreateFileA(
            tempPath,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        )
    );
    if (!hTmp.valid()) {
        logSysError("CreateFileA(perm-test)");
        return;
    }
    hTmp.close(); // CloseHandle – we just need the file on disk
    logInfo("Created temp file for permission demo: " + std::string(tempPath));

    // ── Build DACL with SDDL ─────────────────────────────────────────────────
    // SDDL string: "D:(A;;GA;;;WD)"
    //   D:   – DACL
    //   (A;;GA;;;WD) – Allow (A) GENERIC_ALL (GA) to Everyone (WD = World)
    //
    // ConvertStringSecurityDescriptorToSecurityDescriptorA – Win32 / advapi32
    PSECURITY_DESCRIPTOR pSD = nullptr;
    ULONG sdSize = 0;
    BOOL cvt = ConvertStringSecurityDescriptorToSecurityDescriptorA(
        "D:(A;;GA;;;WD)",   // pStringSecurityDescriptor – SDDL string
        SDDL_REVISION_1,    // StringSDRevision
        &pSD,               // SecurityDescriptor  (must LocalFree)
        &sdSize             // SecurityDescriptorSize (optional)
    );

    if (!cvt || !pSD) {
        logSysError("ConvertStringSecurityDescriptorToSecurityDescriptorA");
        DeleteFileA(tempPath);
        return;
    }
    logInfo("ConvertStringSecurityDescriptorToSecurityDescriptorA: built DACL "
            "(size=" + std::to_string(sdSize) + " B)");

    // SetFileSecurityA – apply the security descriptor to the temp file (Win32)
    BOOL ok = SetFileSecurityA(
        tempPath,
        DACL_SECURITY_INFORMATION,  // SecurityInformation – set the DACL
        pSD
    );

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_ACCESS_DENIED) {
            // Graceful handling – log and continue (matches EPERM handling on Linux)
            logInfo("SetFileSecurityA: privilege not held (err=" +
                    std::to_string(err) + ") – skipping, continuing");
        } else {
            logSysError("SetFileSecurityA");
        }
    } else {
        logInfo("SetFileSecurityA: DACL applied successfully to " +
                std::string(tempPath));
    }

    // LocalFree – release memory allocated by ConvertStringSecurityDescriptor (Win32)
    LocalFree(pSD);
    logInfo("LocalFree: security descriptor memory released");

    // Cleanup temp file
    DeleteFileA(tempPath);
    logInfo("Permission demo complete");
}
