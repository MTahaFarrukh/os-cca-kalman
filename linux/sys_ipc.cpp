/**
 * @file sys_ipc.cpp
 * @brief Raw POSIX IPC system calls: pipe, shmget, shmat, shmdt, shmctl
 * 
 * Part of CT-353 Operating Systems CCA.
 * Implements unidirectional pipes and System V shared memory segments with RAII cleanup.
 */

#ifndef _WIN32
#include "sys_linux.hpp"
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstring>

bool demo_ipc() {
    logInfo("[DEMO] Starting IPC system calls demonstration (pipe, shmget, shmat, shmdt, shmctl)...");

    // ========================================================================
    // Part 1: Pipe Demonstration
    // ========================================================================
    PipePair pipe;
    if (!pipe.create()) {
        return false;
    }

    const char pipe_msg[] = "CT-353 Pipe IPC test packet";
    const size_t msg_len = sizeof(pipe_msg);

    // Write through pipe
    // syscall: write
    ssize_t w_bytes = ::write(pipe.writeFd(), pipe_msg, msg_len);
    if (w_bytes < 0) {
        logSysError("write (pipe)");
        return false;
    }

    // Read through pipe
    char pipe_buf[64] = {0};
    // syscall: read
    ssize_t r_bytes = ::read(pipe.readFd(), pipe_buf, sizeof(pipe_buf));
    if (r_bytes < 0) {
        logSysError("read (pipe)");
        return false;
    }

    pipe.closeWrite();
    pipe.closeRead();

    if (std::strcmp(pipe_msg, pipe_buf) != 0) {
        logError("[DEMO] Pipe data integrity mismatch.");
        return false;
    }
    logInfo("[DEMO] Pipe IPC verified: \"" + std::string(pipe_buf) + "\" (" +
            std::to_string(r_bytes) + " bytes).");

    // ========================================================================
    // Part 2: Shared Memory (shmget, shmat, shmdt, shmctl)
    // ========================================================================
    const size_t shm_size = 4096;

    // 1. Raw syscall: shmget (allocate shared memory segment)
    // syscall: shmget
    int shmid = ::shmget(IPC_PRIVATE, shm_size, IPC_CREAT | 0666);
    if (shmid < 0) {
        logSysError("shmget");
        return false;
    }

    // 2. Raw syscall: shmat (attach shared memory segment)
    // syscall: shmat
    void* shmaddr = ::shmat(shmid, nullptr, 0);
    if (shmaddr == reinterpret_cast<void*>(-1)) {
        logSysError("shmat");
        // Clean up segment before returning
        ::shmctl(shmid, IPC_RMID, nullptr);
        return false;
    }

    SharedMemorySegment shmSegment(shmid, shmaddr, true /* owner */);

    // Write and read from shared memory
    const char shm_data[] = "CCA System V Shared Memory active payload";
    std::memcpy(shmSegment.get(), shm_data, sizeof(shm_data));

    char shm_readback[64] = {0};
    std::memcpy(shm_readback, shmSegment.get(), sizeof(shm_data));

    if (std::strcmp(shm_data, shm_readback) != 0) {
        logError("[DEMO] Shared memory data integrity mismatch.");
        return false;
    }

    // 3. Raw syscalls: shmdt and shmctl (IPC_RMID) handled via RAII reset
    // syscall: shmdt
    // syscall: shmctl
    shmSegment.reset();

    logInfo("[DEMO] Shared memory IPC (shmget/shmat/shmdt/shmctl) successfully verified.");
    return true;
}

#endif // _WIN32
