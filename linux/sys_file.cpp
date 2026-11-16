/**
 * @file sys_file.cpp
 * @brief Raw POSIX File I/O system calls: open, read, write, close
 * 
 * Part of CT-353 Operating Systems CCA.
 * Implemented using strictly raw POSIX system calls with RAII management.
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

bool demo_file_io() {
    logInfo("[DEMO] Starting File I/O system calls demonstration (open, write, read, close)...");
    const char* testPath = "results/demo_file_io.tmp";

    // 1. Raw syscall: open (for writing, creating, truncating)
    // syscall: open
    int raw_fd = ::open(testPath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (raw_fd < 0) {
        logSysError("open");
        return false;
    }
    FileDescriptor fdWrite(raw_fd);

    // 2. Raw syscall: write
    const char payload[] = "Operating Systems CT-353: Raw POSIX direct file I/O demonstration payload.\n";
    const size_t payload_len = sizeof(payload) - 1;

    // syscall: write
    ssize_t bytes_written = ::write(fdWrite.get(), payload, payload_len);
    if (bytes_written < 0) {
        logSysError("write");
        return false;
    }

    // 3. Raw syscall: close (exercised via RAII reset)
    // syscall: close
    fdWrite.reset();

    // 4. Raw syscall: open (read-only)
    // syscall: open
    int read_fd = ::open(testPath, O_RDONLY);
    if (read_fd < 0) {
        logSysError("open");
        return false;
    }
    FileDescriptor fdRead(read_fd);

    // 5. Raw syscall: read
    char read_buffer[128] = {0};
    // syscall: read
    ssize_t bytes_read = ::read(fdRead.get(), read_buffer, sizeof(read_buffer) - 1);
    if (bytes_read < 0) {
        logSysError("read");
        return false;
    }

    // 6. Raw syscall: close
    // syscall: close
    fdRead.reset();

    // Clean up temporary demonstration file
    ::unlink(testPath);

    logInfo("[DEMO] File I/O system calls successfully verified. Bytes read: " + std::to_string(bytes_read));
    return true;
}

#endif // _WIN32
