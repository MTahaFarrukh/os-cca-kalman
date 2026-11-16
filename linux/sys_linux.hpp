#ifndef OS_CCA_SYS_LINUX_HPP
#define OS_CCA_SYS_LINUX_HPP

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/resource.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <string>
#include <utility>

#include "../common/logger.hpp"

// ============================================================================
// RAII Wrappers for Raw POSIX System Calls
// ============================================================================

/**
 * @brief RAII wrapper for raw POSIX file descriptors.
 * Ensures ::close() is executed on destruction.
 */
class FileDescriptor {
public:
    FileDescriptor() noexcept : fd_(-1) {}
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
    ~FileDescriptor() {
        reset();
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool isValid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return isValid(); }

    void reset(int new_fd = -1) noexcept {
        if (fd_ >= 0) {
            // raw syscall: close
            ::close(fd_);
        }
        fd_ = new_fd;
    }

    [[nodiscard]] int release() noexcept {
        int temp = fd_;
        fd_ = -1;
        return temp;
    }

private:
    int fd_;
};

/**
 * @brief RAII wrapper for memory-mapped pages (mmap / munmap).
 */
class MmapRegion {
public:
    MmapRegion() noexcept : addr_(MAP_FAILED), length_(0) {}
    MmapRegion(void* addr, size_t length) noexcept : addr_(addr), length_(length) {}
    ~MmapRegion() {
        reset();
    }

    MmapRegion(const MmapRegion&) = delete;
    MmapRegion& operator=(const MmapRegion&) = delete;

    MmapRegion(MmapRegion&& other) noexcept : addr_(other.addr_), length_(other.length_) {
        other.addr_ = MAP_FAILED;
        other.length_ = 0;
    }
    MmapRegion& operator=(MmapRegion&& other) noexcept {
        if (this != &other) {
            reset();
            addr_ = other.addr_;
            length_ = other.length_;
            other.addr_ = MAP_FAILED;
            other.length_ = 0;
        }
        return *this;
    }

    [[nodiscard]] void* get() const noexcept { return addr_; }
    [[nodiscard]] size_t size() const noexcept { return length_; }
    [[nodiscard]] bool isValid() const noexcept { return addr_ != MAP_FAILED && addr_ != nullptr; }
    explicit operator bool() const noexcept { return isValid(); }

    void reset(void* new_addr = MAP_FAILED, size_t new_len = 0) noexcept {
        if (isValid() && length_ > 0) {
            // raw syscall: munmap
            ::munmap(addr_, length_);
        }
        addr_ = new_addr;
        length_ = new_len;
    }

private:
    void* addr_;
    size_t length_;
};

/**
 * @brief RAII wrapper for System V Shared Memory segment (shmget / shmat / shmdt / shmctl).
 */
class SharedMemorySegment {
public:
    SharedMemorySegment() noexcept : shmid_(-1), addr_(reinterpret_cast<void*>(-1)), owner_(false) {}
    SharedMemorySegment(int shmid, void* addr, bool owner) noexcept
        : shmid_(shmid), addr_(addr), owner_(owner) {}
    ~SharedMemorySegment() {
        reset();
    }

    SharedMemorySegment(const SharedMemorySegment&) = delete;
    SharedMemorySegment& operator=(const SharedMemorySegment&) = delete;

    SharedMemorySegment(SharedMemorySegment&& other) noexcept
        : shmid_(other.shmid_), addr_(other.addr_), owner_(other.owner_) {
        other.shmid_ = -1;
        other.addr_ = reinterpret_cast<void*>(-1);
        other.owner_ = false;
    }
    SharedMemorySegment& operator=(SharedMemorySegment&& other) noexcept {
        if (this != &other) {
            reset();
            shmid_ = other.shmid_;
            addr_ = other.addr_;
            owner_ = other.owner_;
            other.shmid_ = -1;
            other.addr_ = reinterpret_cast<void*>(-1);
            other.owner_ = false;
        }
        return *this;
    }

    [[nodiscard]] void* get() const noexcept { return addr_; }
    [[nodiscard]] int getId() const noexcept { return shmid_; }
    [[nodiscard]] bool isValid() const noexcept { return addr_ != reinterpret_cast<void*>(-1) && addr_ != nullptr; }

    void reset() noexcept {
        if (addr_ != reinterpret_cast<void*>(-1) && addr_ != nullptr) {
            // raw syscall: shmdt
            ::shmdt(addr_);
            addr_ = reinterpret_cast<void*>(-1);
        }
        if (owner_ && shmid_ >= 0) {
            // raw syscall: shmctl (IPC_RMID)
            ::shmctl(shmid_, IPC_RMID, nullptr);
            shmid_ = -1;
            owner_ = false;
        }
    }

private:
    int shmid_;
    void* addr_;
    bool owner_;
};

/**
 * @brief RAII wrapper for an anonymous unidirectional pipe.
 */
class PipePair {
public:
    PipePair() noexcept {
        fds_[0] = -1;
        fds_[1] = -1;
    }
    ~PipePair() {
        closeRead();
        closeWrite();
    }

    PipePair(const PipePair&) = delete;
    PipePair& operator=(const PipePair&) = delete;

    bool create() {
        // raw syscall: pipe
        if (::pipe(fds_) != 0) {
            logSysError("pipe");
            return false;
        }
        return true;
    }

    [[nodiscard]] int readFd() const noexcept { return fds_[0]; }
    [[nodiscard]] int writeFd() const noexcept { return fds_[1]; }

    void closeRead() noexcept {
        if (fds_[0] >= 0) {
            // raw syscall: close
            ::close(fds_[0]);
            fds_[0] = -1;
        }
    }

    void closeWrite() noexcept {
        if (fds_[1] >= 0) {
            // raw syscall: close
            ::close(fds_[1]);
            fds_[1] = -1;
        }
    }

private:
    int fds_[2];
};

// ============================================================================
// Demos for the 15 system calls
// ============================================================================
bool demo_file_io();
bool demo_proc();
bool demo_mem();
bool demo_ipc();
bool demo_perm();

#endif // _WIN32
#endif // OS_CCA_SYS_LINUX_HPP
