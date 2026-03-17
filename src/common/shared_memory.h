#pragma once

#include <cstdint>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "util.h"

namespace ox {
namespace protocol {

// Cross-platform shared memory wrapper
class SharedMemory {
   public:
    SharedMemory() : ptr_(nullptr), size_(0) {
#ifdef _WIN32
        handle_ = nullptr;
#else
        fd_ = -1;
#endif
    }

    ~SharedMemory() { Close(); }

    // Create or open shared memory region
    bool Create(const char* name, size_t size, bool create_new = true) {
        size_ = size;

#ifdef _WIN32
        SECURITY_ATTRIBUTES* sa = create_new ? CreateOwnerOnlySecurityAttributes() : nullptr;

        if (create_new) {
            handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, sa, PAGE_READWRITE, 0, static_cast<DWORD>(size), name);
        } else {
            handle_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        }

        if (!handle_) {
            DWORD error = GetLastError();
            fprintf(stderr, "SharedMemory::%s failed for '%s': Error %lu (size=%zu)\n",
                    create_new ? "CreateFileMapping" : "OpenFileMapping", name, error, size);
            fflush(stderr);
            return false;
        }

        if (create_new) {
            fprintf(stderr, "SharedMemory::Created '%s' with size %zu bytes\n", name, size);
            fflush(stderr);
        } else {
            fprintf(stderr, "SharedMemory::Opened '%s' expecting size %zu bytes\n", name, size);
            fflush(stderr);
        }

        ptr_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!ptr_) {
            DWORD error = GetLastError();
            fprintf(stderr, "SharedMemory::MapViewOfFile failed for '%s': Error %lu (size=%zu)\n", name, error, size);
            CloseHandle(handle_);
            handle_ = nullptr;
            return false;
        }

        // Zero initialize if newly created
        if (create_new && GetLastError() != ERROR_ALREADY_EXISTS) {
            memset(ptr_, 0, size);
        }

#else
        int flags = O_RDWR;
        if (create_new) {
            flags |= O_CREAT;
        }

        // Restrict to owner only (0600) for security
        fd_ = shm_open(name, flags, 0600);
        if (fd_ == -1) {
            return false;
        }

        if (create_new) {
            if (ftruncate(fd_, size) == -1) {
                close(fd_);
                fd_ = -1;
                return false;
            }
        }

        ptr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            close(fd_);
            fd_ = -1;
            ptr_ = nullptr;
            return false;
        }

        // Zero initialize if newly created
        if (create_new) {
            memset(ptr_, 0, size);
        }
#endif

        return true;
    }

    void Close() {
        if (ptr_) {
#ifdef _WIN32
            UnmapViewOfFile(ptr_);
            ptr_ = nullptr;
#else
            munmap(ptr_, size_);
            ptr_ = nullptr;
#endif
        }

#ifdef _WIN32
        if (handle_) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#else
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }
#endif
    }

    void* GetPointer() const { return ptr_; }
    size_t GetSize() const { return size_; }

   private:
    void* ptr_;
    size_t size_;

#ifdef _WIN32
    HANDLE handle_;
#else
    int fd_;
#endif
};

// Unlink shared memory (call from service on shutdown)
inline void UnlinkSharedMemory(const char* name) {
#ifndef _WIN32
    shm_unlink(name);
#endif
}

}  // namespace protocol
}  // namespace ox
