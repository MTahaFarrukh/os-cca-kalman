/**
 * @file sys_perm.cpp
 * @brief Raw POSIX Permissions system calls: chmod, chown, umask
 * 
 * Part of CT-353 Operating Systems CCA.
 * Implements mode bit changes, ownership inspection, and permission mask adjustments.
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

bool demo_perm() {
    logInfo("[DEMO] Starting Permissions system calls demonstration (umask, chmod, chown)...");
    const char* testFile = "results/demo_perm.tmp";

    // 1. Raw syscall: umask
    // syscall: umask
    mode_t old_mask = ::umask(0022);
    logInfo("[DEMO] Previous umask was: 0" + std::to_string(old_mask));

    // Restore or set working umask
    // syscall: umask
    ::umask(old_mask);

    // Create a temporary file to demonstrate chmod and chown
    // syscall: open
    int raw_fd = ::open(testFile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (raw_fd < 0) {
        logSysError("open");
        return false;
    }
    FileDescriptor fd(raw_fd);
    fd.reset(); // close file descriptor before chmod/chown

    // 2. Raw syscall: chmod
    // syscall: chmod
    int chmod_res = ::chmod(testFile, S_IRUSR | S_IWUSR); // 0600
    if (chmod_res != 0) {
        logSysError("chmod");
        ::unlink(testFile);
        return false;
    }
    logInfo("[DEMO] chmod successfully set permissions to 0600 on " + std::string(testFile));

    // 3. Raw syscall: chown
    // Note: chown to another user typically requires CAP_CHOWN or root.
    // Changing to current euid/egid is safe, but we also handle EPERM gracefully.
    uid_t current_uid = ::geteuid();
    gid_t current_gid = ::getegid();

    // syscall: chown
    int chown_res = ::chown(testFile, current_uid, current_gid);
    if (chown_res != 0) {
        if (errno == EPERM) {
            logInfo("[DEMO] chown encountered EPERM (operation not permitted for non-root caller). Gracefully handled.");
        } else {
            logSysError("chown");
        }
    } else {
        logInfo("[DEMO] chown successfully confirmed ownership for UID=" +
                std::to_string(current_uid) + ", GID=" + std::to_string(current_gid));
    }

    // Clean up temporary file
    ::unlink(testFile);
    logInfo("[DEMO] Permission system calls (umask, chmod, chown) successfully verified.");
    return true;
}

#endif // _WIN32
