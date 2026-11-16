/**
 * @file sys_win.hpp
 * @brief RAII wrappers for Win32 HANDLEs and virtual memory regions.
 *
 * All wrappers are non-copyable, move-only, and release their resources
 * in the destructor using the appropriate Win32 cleanup call.
 */

#ifndef OS_CCA_SYS_WIN_HPP
#define OS_CCA_SYS_WIN_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <utility>  // std::exchange

// ─────────────────────────────────────────────────────────────────────────────
// 1.  HandleGuard
//     Wraps any HANDLE that must be closed with CloseHandle().
//     Treats both INVALID_HANDLE_VALUE and NULL as "no handle".
// ─────────────────────────────────────────────────────────────────────────────
class HandleGuard {
public:
    HandleGuard() noexcept : h_(INVALID_HANDLE_VALUE) {}
    explicit HandleGuard(HANDLE h) noexcept : h_(h) {}

    // Move constructor / assignment
    HandleGuard(HandleGuard&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    HandleGuard& operator=(HandleGuard&& o) noexcept {
        if (this != &o) {
            close();
            h_ = std::exchange(o.h_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    // No copies
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    ~HandleGuard() { close(); }

    [[nodiscard]] HANDLE get()   const noexcept { return h_; }
    [[nodiscard]] bool   valid() const noexcept {
        return h_ != INVALID_HANDLE_VALUE && h_ != nullptr;
    }

    /** Release ownership without closing. */
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }

    /** CloseHandle() the wrapped HANDLE. */
    void close() noexcept {
        if (valid()) {
            CloseHandle(h_);          // CloseHandle – Win32 cleanup
            h_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE h_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 2.  VirtualMemory
//     Wraps memory allocated by VirtualAlloc(); frees via VirtualFree().
// ─────────────────────────────────────────────────────────────────────────────
class VirtualMemory {
public:
    VirtualMemory() noexcept : ptr_(nullptr), size_(0) {}
    VirtualMemory(void* p, SIZE_T sz) noexcept : ptr_(p), size_(sz) {}

    VirtualMemory(VirtualMemory&& o) noexcept
        : ptr_(std::exchange(o.ptr_, nullptr)),
          size_(std::exchange(o.size_, 0)) {}
    VirtualMemory& operator=(VirtualMemory&& o) noexcept {
        if (this != &o) {
            free();
            ptr_  = std::exchange(o.ptr_,  nullptr);
            size_ = std::exchange(o.size_, 0);
        }
        return *this;
    }

    VirtualMemory(const VirtualMemory&) = delete;
    VirtualMemory& operator=(const VirtualMemory&) = delete;

    ~VirtualMemory() { free(); }

    [[nodiscard]] void*  get()   const noexcept { return ptr_; }
    [[nodiscard]] SIZE_T size()  const noexcept { return size_; }
    [[nodiscard]] bool   valid() const noexcept { return ptr_ != nullptr; }

    void free() noexcept {
        if (ptr_) {
            VirtualFree(ptr_, 0, MEM_RELEASE);  // VirtualFree – Win32 release
            ptr_  = nullptr;
            size_ = 0;
        }
    }

private:
    void*  ptr_;
    SIZE_T size_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 3.  FileMappingView
//     Wraps a view obtained from MapViewOfFile(); unmaps via UnmapViewOfFile().
// ─────────────────────────────────────────────────────────────────────────────
class FileMappingView {
public:
    FileMappingView() noexcept : ptr_(nullptr) {}
    explicit FileMappingView(void* p) noexcept : ptr_(p) {}

    FileMappingView(FileMappingView&& o) noexcept
        : ptr_(std::exchange(o.ptr_, nullptr)) {}
    FileMappingView& operator=(FileMappingView&& o) noexcept {
        if (this != &o) {
            unmap();
            ptr_ = std::exchange(o.ptr_, nullptr);
        }
        return *this;
    }

    FileMappingView(const FileMappingView&) = delete;
    FileMappingView& operator=(const FileMappingView&) = delete;

    ~FileMappingView() { unmap(); }

    [[nodiscard]] void* get()   const noexcept { return ptr_; }
    [[nodiscard]] bool  valid() const noexcept { return ptr_ != nullptr; }

    template<typename T>
    [[nodiscard]] T* as() const noexcept { return static_cast<T*>(ptr_); }

    void unmap() noexcept {
        if (ptr_) {
            UnmapViewOfFile(ptr_);   // UnmapViewOfFile – Win32 cleanup
            ptr_ = nullptr;
        }
    }

private:
    void* ptr_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 4.  PipePairWin
//     Wraps an anonymous pipe (read end + write end).
//     Closes both ends on destruction via CloseHandle().
// ─────────────────────────────────────────────────────────────────────────────
struct PipePairWin {
    HandleGuard read_end;
    HandleGuard write_end;

    PipePairWin() = default;
    PipePairWin(HANDLE r, HANDLE w) noexcept
        : read_end(r), write_end(w) {}

    [[nodiscard]] bool valid() const noexcept {
        return read_end.valid() && write_end.valid();
    }
};

#endif // OS_CCA_SYS_WIN_HPP
