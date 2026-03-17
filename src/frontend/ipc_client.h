#pragma once

#include <ox_driver.h>

#include <mutex>
#include <string>
#include <vector>

#include "../common/control_channel.h"
#include "../common/messages.h"
#include "../common/shared_memory.h"

namespace ox {
namespace ipc {

class IpcClient {
   public:
    static IpcClient& Instance();

    bool Connect();
    void Disconnect();
    bool IsConnected() const;

    void GetDeviceInfo(OxDeviceInfo* info) const;
    void GetDisplayProperties(OxDisplayProperties* props) const;
    void GetTrackingCapabilities(OxTrackingCapabilities* caps) const;
    void UpdateViewPose(int64_t predicted_time, uint32_t eye_index, OxPose* out_pose);
    void UpdateDevices(int64_t predicted_time, OxDeviceState* out_states, uint32_t* out_count);
    OxComponentResult GetInputStateBoolean(int64_t predicted_time, const char* user_path, const char* component_path,
                                           uint32_t* out_value);
    OxComponentResult GetInputStateFloat(int64_t predicted_time, const char* user_path, const char* component_path,
                                         float* out_value);
    OxComponentResult GetInputStateVector2f(int64_t predicted_time, const char* user_path, const char* component_path,
                                            OxVector2f* out_value);
    uint32_t GetInteractionProfiles(const char** profiles, uint32_t max_profiles) const;
    void NotifySessionState(OxSessionState state);
    void SubmitFramePixels(uint32_t eye_index, uint32_t width, uint32_t height, uint32_t format, const void* pixel_data,
                           uint32_t data_size);

   private:
    IpcClient();
    ~IpcClient();
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    template <typename ResponseType>
    bool SendRequest(MessageType type, const void* payload, uint32_t payload_size, ResponseType* response);
    template <typename ResponseType>
    bool SendRequestLocked(MessageType type, const void* payload, uint32_t payload_size, ResponseType* response);
    bool QueryMetadata();

    mutable std::mutex mutex_;
    protocol::SharedMemory shared_memory_;
    protocol::ControlChannel control_channel_;
    SharedData* shared_data_;
    bool connected_;
    uint32_t sequence_;
    OxDeviceInfo device_info_;
    OxDisplayProperties display_props_;
    OxTrackingCapabilities tracking_caps_;
    InteractionProfilesResponse interaction_profiles_;
    std::vector<std::string> interaction_profile_storage_;
};

}  // namespace ipc
}  // namespace ox