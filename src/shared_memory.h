#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ox {
namespace protocol {

#ifdef _WIN32
inline SECURITY_ATTRIBUTES* CreateSharedMemorySecurityAttributes() {
    static SECURITY_ATTRIBUTES sa{};
    static bool initialized = false;

    if (initialized) {
        return &sa;
    }

    const char* sddl = "D:P(A;;GA;;;WD)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor,
                                                              nullptr)) {
        return nullptr;
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    initialized = true;
    return &sa;
}
#endif

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

    const std::string& GetLastError() const { return last_error_; }

    // Create or open shared memory region
    bool Create(const char* name, size_t size, bool create_new = true) {
        size_ = size;
        last_error_.clear();

#ifdef _WIN32
        SECURITY_ATTRIBUTES* sa = create_new ? CreateSharedMemorySecurityAttributes() : nullptr;

        if (create_new) {
            handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, sa, PAGE_READWRITE, 0, static_cast<DWORD>(size), name);
        } else {
            handle_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
        }

        if (!handle_) {
            DWORD error = GetLastError();
            last_error_ = "SharedMemory::" + std::string(create_new ? "CreateFileMapping" : "OpenFileMapping") +
                          " failed for '" + name + "': Error " + std::to_string(error) +
                          " (size=" + std::to_string(size) + ")";
            fprintf(stderr, "%s\n", last_error_.c_str());
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
            last_error_ = "SharedMemory::MapViewOfFile failed for '" + std::string(name) + "': Error " +
                          std::to_string(error) + " (size=" + std::to_string(size) + ")";
            fprintf(stderr, "%s\n", last_error_.c_str());
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
            last_error_ = "SharedMemory::shm_open failed for '" + std::string(name) + "': errno " +
                          std::to_string(errno) + " (" + std::strerror(errno) + ")" +
                          " (size=" + std::to_string(size) + ")";
            fprintf(stderr, "%s\n", last_error_.c_str());
            fflush(stderr);
            return false;
        }

        if (create_new) {
            if (ftruncate(fd_, size) == -1) {
                last_error_ = "SharedMemory::ftruncate failed for '" + std::string(name) + "': errno " +
                              std::to_string(errno) + " (" + std::strerror(errno) + ")" +
                              " (size=" + std::to_string(size) + ")";
                fprintf(stderr, "%s\n", last_error_.c_str());
                fflush(stderr);
                close(fd_);
                fd_ = -1;
                return false;
            }
        }

        ptr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            last_error_ = "SharedMemory::mmap failed for '" + std::string(name) + "': errno " +
                          std::to_string(errno) + " (" + std::strerror(errno) + ")" +
                          " (size=" + std::to_string(size) + ")";
            fprintf(stderr, "%s\n", last_error_.c_str());
            fflush(stderr);
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
            if (munmap(ptr_, size_) != 0) {
                fprintf(stderr, "SharedMemory::munmap failed: errno %d (%s)\n", errno, std::strerror(errno));
                fflush(stderr);
            }
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
    std::string last_error_;

#ifdef _WIN32
    HANDLE handle_;
#else
    int fd_;
#endif
};

// Unlink shared memory (call from service on shutdown)
inline void UnlinkSharedMemory(const char* name) {
#ifndef _WIN32
    if (shm_unlink(name) != 0 && errno != ENOENT) {
        fprintf(stderr, "SharedMemory::shm_unlink failed for '%s': errno %d (%s)\n", name, errno, std::strerror(errno));
        fflush(stderr);
    }
#endif
}

}  // namespace protocol
}  // namespace ox
