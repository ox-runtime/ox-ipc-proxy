#pragma once

#include <ox_driver.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "../common/control_channel.h"
#include "../common/messages.h"
#include "../common/shared_memory.h"

namespace ox {
namespace ipc {

class IpcServer {
   public:
    static IpcServer& Instance();

    void SetDriver(const OxDriverCallbacks* callbacks);
    bool Initialize();
    void Shutdown();

   private:
    IpcServer();
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    void ServerLoop();
    void MessageLoop();
    void FrameLoop();
    void InitializeSharedData();
    void HandleConnect(const MessageHeader& request);
    void HandleMetadataRequest(const MessageHeader& request, MessageType type);
    void HandleInputRequest(const MessageHeader& request, const std::vector<uint8_t>& payload);
    void HandleSessionState(const MessageHeader& request, const std::vector<uint8_t>& payload);

    mutable std::mutex mutex_;
    OxDriverCallbacks driver_callbacks_;
    bool driver_set_;
    std::atomic<bool> running_;
    protocol::SharedMemory shared_memory_;
    protocol::ControlChannel control_channel_;
    SharedData* shared_data_;
    std::thread server_thread_;
    std::thread frame_thread_;
    std::atomic<uint64_t> frame_counter_;
};

}  // namespace ipc
}  // namespace ox