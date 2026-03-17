#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "messages.h"
#include "util.h"

namespace ox {
namespace protocol {

using ::ox::ipc::MessageHeader;

// Control channel for lifecycle and configuration messages
class ControlChannel {
   public:
    ControlChannel() {
#ifdef _WIN32
        pipe_ = INVALID_HANDLE_VALUE;
#else
        sock_ = -1;
#endif
    }

    ~ControlChannel() { Close(); }

    // Server: Create and listen
    bool CreateServer(const char* name) {
#ifdef _WIN32
        char pipe_name[256];
        snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\%s", name);

        SECURITY_ATTRIBUTES* sa = CreateOwnerOnlySecurityAttributes();
        pipe_ = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                 1, 4096, 4096, 0, sa);

        return pipe_ != INVALID_HANDLE_VALUE;
#else
        sock_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_ == -1) {
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/%s.sock", name);

        // Remove existing socket file
        unlink(addr.sun_path);

        if (bind(sock_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            close(sock_);
            sock_ = -1;
            return false;
        }

        // Set restrictive permissions (owner only) for security
        chmod(addr.sun_path, 0600);

        if (listen(sock_, 1) == -1) {
            close(sock_);
            sock_ = -1;
            return false;
        }

        return true;
#endif
    }

    // Server: Accept connection (blocking)
    bool Accept() {
#ifdef _WIN32
        return ConnectNamedPipe(pipe_, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
#else
        int client_sock = accept(sock_, nullptr, nullptr);
        if (client_sock == -1) {
            return false;
        }
        // Replace listen socket with connected socket
        close(sock_);
        sock_ = client_sock;
        return true;
#endif
    }

    // Client: Connect to server
    bool Connect(const char* name, int timeout_ms = 5000) {
#ifdef _WIN32
        char pipe_name[256];
        snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\%s", name);

        DWORD start = GetTickCount();
        while (true) {
            pipe_ = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);

            if (pipe_ != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_MESSAGE;
                SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);
                return true;
            }

            if (GetLastError() != ERROR_PIPE_BUSY) {
                return false;
            }

            if (GetTickCount() - start > (DWORD)timeout_ms) {
                return false;
            }

            Sleep(10);
        }
#else
        sock_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_ == -1) {
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/%s.sock", name);

        // Retry for timeout period
        int attempts = timeout_ms / 100;
        for (int i = 0; i < attempts; i++) {
            if (connect(sock_, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                return true;
            }
            usleep(100000);  // 100ms
        }

        close(sock_);
        sock_ = -1;
        return false;
#endif
    }

    // Send message
    bool Send(const MessageHeader& header, const void* payload = nullptr) {
#ifdef _WIN32
        DWORD written;
        if (!WriteFile(pipe_, &header, sizeof(header), &written, nullptr)) {
            return false;
        }
        if (payload && header.payload_size > 0) {
            if (!WriteFile(pipe_, payload, header.payload_size, &written, nullptr)) {
                return false;
            }
        }
        return true;
#else
        if (send(sock_, &header, sizeof(header), 0) != sizeof(header)) {
            return false;
        }
        if (payload && header.payload_size > 0) {
            if (send(sock_, payload, header.payload_size, 0) != (ssize_t)header.payload_size) {
                return false;
            }
        }
        return true;
#endif
    }

    // Receive message
    bool Receive(MessageHeader& header, std::vector<uint8_t>& payload) {
#ifdef _WIN32
        DWORD read;
        if (!ReadFile(pipe_, &header, sizeof(header), &read, nullptr) || read != sizeof(header)) {
            return false;
        }
        if (header.payload_size > 0) {
            payload.resize(header.payload_size);
            if (!ReadFile(pipe_, payload.data(), header.payload_size, &read, nullptr)) {
                return false;
            }
        }
        return true;
#else
        if (recv(sock_, &header, sizeof(header), MSG_WAITALL) != sizeof(header)) {
            return false;
        }
        if (header.payload_size > 0) {
            payload.resize(header.payload_size);
            if (recv(sock_, payload.data(), header.payload_size, MSG_WAITALL) != (ssize_t)header.payload_size) {
                return false;
            }
        }
        return true;
#endif
    }

    void Close() {
#ifdef _WIN32
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
#else
        if (sock_ != -1) {
            close(sock_);
            sock_ = -1;
        }
#endif
    }

   private:
#ifdef _WIN32
    HANDLE pipe_;
#else
    int sock_;
#endif
};

}  // namespace protocol
}  // namespace ox
