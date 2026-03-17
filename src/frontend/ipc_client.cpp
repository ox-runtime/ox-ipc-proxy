#include "ipc_client.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

namespace ox {
namespace ipc {

namespace {

template <typename T>
void SafeCopyStruct(T* dest, const T& src) {
    if (dest) {
        *dest = src;
    }
}

}  // namespace

IpcClient& IpcClient::Instance() {
    static IpcClient instance;
    return instance;
}

IpcClient::IpcClient()
    : shared_data_(nullptr),
      connected_(false),
      sequence_(1),
      device_info_{},
      display_props_{},
      tracking_caps_{},
      interaction_profiles_{} {}

IpcClient::~IpcClient() { Disconnect(); }

bool IpcClient::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        return true;
    }

    if (!shared_memory_.Create(SHARED_MEMORY_NAME, sizeof(SharedData), false)) {
        spdlog::error("Failed to open IPC shared memory");
        return false;
    }

    shared_data_ = static_cast<SharedData*>(shared_memory_.GetPointer());
    if (!shared_data_) {
        spdlog::error("IPC shared memory pointer is null");
        shared_memory_.Close();
        return false;
    }

    if (shared_data_->protocol_version.load(std::memory_order_acquire) != PROTOCOL_VERSION) {
        spdlog::error("IPC protocol version mismatch");
        shared_memory_.Close();
        shared_data_ = nullptr;
        return false;
    }

    if (!control_channel_.Connect(CONTROL_CHANNEL_NAME, 5000)) {
        spdlog::error("Failed to connect to IPC control channel");
        shared_memory_.Close();
        shared_data_ = nullptr;
        return false;
    }

    MessageHeader header{};
    header.type = MessageType::CONNECT;
    header.sequence = sequence_++;
    header.payload_size = 0;

    if (!control_channel_.Send(header)) {
        spdlog::error("Failed to send IPC connect request");
        control_channel_.Close();
        shared_memory_.Close();
        shared_data_ = nullptr;
        return false;
    }

    MessageHeader response{};
    std::vector<uint8_t> payload;
    if (!control_channel_.Receive(response, payload) || response.type != MessageType::RESPONSE) {
        spdlog::error("Failed to receive IPC connect response");
        control_channel_.Close();
        shared_memory_.Close();
        shared_data_ = nullptr;
        return false;
    }

    connected_ = true;

    if (!QueryMetadata()) {
        spdlog::error("Failed to query IPC metadata");
        connected_ = false;
        control_channel_.Close();
        shared_memory_.Close();
        shared_data_ = nullptr;
        return false;
    }

    shared_data_->frontend_connected.store(1, std::memory_order_release);
    return true;
}

void IpcClient::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) {
        return;
    }

    MessageHeader header{};
    header.type = MessageType::DISCONNECT;
    header.sequence = sequence_++;
    header.payload_size = 0;
    control_channel_.Send(header);

    if (shared_data_) {
        shared_data_->frontend_connected.store(0, std::memory_order_release);
    }

    control_channel_.Close();
    shared_memory_.Close();
    shared_data_ = nullptr;
    connected_ = false;
}

bool IpcClient::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

void IpcClient::GetDeviceInfo(OxDeviceInfo* info) const { SafeCopyStruct(info, device_info_); }

void IpcClient::GetDisplayProperties(OxDisplayProperties* props) const { SafeCopyStruct(props, display_props_); }

void IpcClient::GetTrackingCapabilities(OxTrackingCapabilities* caps) const { SafeCopyStruct(caps, tracking_caps_); }

void IpcClient::UpdateViewPose(int64_t predicted_time, uint32_t eye_index, OxPose* out_pose) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !shared_data_ || !out_pose || eye_index >= 2) {
        return;
    }

    shared_data_->frame_state.predicted_display_time.store(predicted_time, std::memory_order_release);
    const auto& view = shared_data_->frame_state.views[eye_index];
    *out_pose = view.pose.pose;
}

void IpcClient::UpdateDevices(int64_t predicted_time, OxDeviceState* out_states, uint32_t* out_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !shared_data_ || !out_count) {
        if (out_count) {
            *out_count = 0;
        }
        return;
    }

    shared_data_->frame_state.predicted_display_time.store(predicted_time, std::memory_order_release);
    const uint32_t count =
        std::min<uint32_t>(shared_data_->frame_state.device_count.load(std::memory_order_acquire), OX_MAX_DEVICES);
    *out_count = count;
    if (!out_states) {
        return;
    }

    for (uint32_t index = 0; index < count; ++index) {
        snprintf(out_states[index].user_path, sizeof(out_states[index].user_path), "%s",
                 shared_data_->frame_state.device_states[index].user_path);
        out_states[index].pose = shared_data_->frame_state.device_states[index].pose.pose;
        out_states[index].is_active = shared_data_->frame_state.device_states[index].is_active;
    }
}

OxComponentResult IpcClient::GetInputStateBoolean(int64_t predicted_time, const char* user_path,
                                                  const char* component_path, uint32_t* out_value) {
    InputStateRequest request{};
    request.predicted_time = predicted_time;
    snprintf(request.user_path, sizeof(request.user_path), "%s", user_path);
    snprintf(request.component_path, sizeof(request.component_path), "%s", component_path);

    InputStateBooleanResponse response{};
    if (!SendRequest(MessageType::GET_INPUT_STATE_BOOLEAN, &request, sizeof(request), &response)) {
        return OX_COMPONENT_UNAVAILABLE;
    }

    if (out_value) {
        *out_value = response.value;
    }
    return response.is_available ? OX_COMPONENT_AVAILABLE : OX_COMPONENT_UNAVAILABLE;
}

OxComponentResult IpcClient::GetInputStateFloat(int64_t predicted_time, const char* user_path,
                                                const char* component_path, float* out_value) {
    InputStateRequest request{};
    request.predicted_time = predicted_time;
    snprintf(request.user_path, sizeof(request.user_path), "%s", user_path);
    snprintf(request.component_path, sizeof(request.component_path), "%s", component_path);

    InputStateFloatResponse response{};
    if (!SendRequest(MessageType::GET_INPUT_STATE_FLOAT, &request, sizeof(request), &response)) {
        return OX_COMPONENT_UNAVAILABLE;
    }

    if (out_value) {
        *out_value = response.value;
    }
    return response.is_available ? OX_COMPONENT_AVAILABLE : OX_COMPONENT_UNAVAILABLE;
}

OxComponentResult IpcClient::GetInputStateVector2f(int64_t predicted_time, const char* user_path,
                                                   const char* component_path, OxVector2f* out_value) {
    InputStateRequest request{};
    request.predicted_time = predicted_time;
    snprintf(request.user_path, sizeof(request.user_path), "%s", user_path);
    snprintf(request.component_path, sizeof(request.component_path), "%s", component_path);

    InputStateVector2fResponse response{};
    if (!SendRequest(MessageType::GET_INPUT_STATE_VECTOR2F, &request, sizeof(request), &response)) {
        return OX_COMPONENT_UNAVAILABLE;
    }

    if (out_value) {
        *out_value = response.value;
    }
    return response.is_available ? OX_COMPONENT_AVAILABLE : OX_COMPONENT_UNAVAILABLE;
}

uint32_t IpcClient::GetInteractionProfiles(const char** profiles, uint32_t max_profiles) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(interaction_profile_storage_.size()), max_profiles);
    for (uint32_t index = 0; index < count; ++index) {
        profiles[index] = interaction_profile_storage_[index].c_str();
    }
    return static_cast<uint32_t>(interaction_profile_storage_.size());
}

void IpcClient::NotifySessionState(OxSessionState state) {
    SessionStateNotification notification{state};
    SendRequest<MessageHeader>(MessageType::NOTIFY_SESSION_STATE, &notification, sizeof(notification), nullptr);
}

void IpcClient::SubmitFramePixels(uint32_t eye_index, uint32_t width, uint32_t height, uint32_t format,
                                  const void* pixel_data, uint32_t data_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !shared_data_ || !pixel_data || eye_index >= 2 || data_size > MAX_TEXTURE_SIZE) {
        return;
    }

    auto& texture = shared_data_->frame_state.textures[eye_index];
    texture.ready.store(0, std::memory_order_release);
    texture.width.store(width, std::memory_order_relaxed);
    texture.height.store(height, std::memory_order_relaxed);
    texture.format.store(format, std::memory_order_relaxed);
    texture.data_size.store(data_size, std::memory_order_relaxed);
    std::memcpy(texture.pixel_data, pixel_data, data_size);
    texture.ready.store(1, std::memory_order_release);
}

template <typename ResponseType>
bool IpcClient::SendRequest(MessageType type, const void* payload, uint32_t payload_size, ResponseType* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    return SendRequestLocked(type, payload, payload_size, response);
}

template <typename ResponseType>
bool IpcClient::SendRequestLocked(MessageType type, const void* payload, uint32_t payload_size,
                                  ResponseType* response) {
    if (!connected_) {
        return false;
    }

    MessageHeader header{};
    header.type = type;
    header.sequence = sequence_++;
    header.payload_size = payload_size;

    if (!control_channel_.Send(header, payload)) {
        return false;
    }

    MessageHeader reply{};
    std::vector<uint8_t> reply_payload;
    if (!control_channel_.Receive(reply, reply_payload) || reply.type != MessageType::RESPONSE) {
        return false;
    }

    if constexpr (!std::is_same_v<ResponseType, MessageHeader>) {
        if (!response) {
            return true;
        }
        if (reply_payload.size() < sizeof(ResponseType)) {
            return false;
        }
        std::memcpy(response, reply_payload.data(), sizeof(ResponseType));
    }

    return true;
}

bool IpcClient::QueryMetadata() {
    if (!SendRequestLocked(MessageType::GET_DEVICE_INFO, nullptr, 0, &device_info_)) {
        return false;
    }
    if (!SendRequestLocked(MessageType::GET_DISPLAY_PROPERTIES, nullptr, 0, &display_props_)) {
        return false;
    }
    if (!SendRequestLocked(MessageType::GET_TRACKING_CAPABILITIES, nullptr, 0, &tracking_caps_)) {
        return false;
    }
    if (!SendRequestLocked(MessageType::GET_INTERACTION_PROFILES, nullptr, 0, &interaction_profiles_)) {
        return false;
    }

    interaction_profile_storage_.clear();
    const uint32_t count = std::min<uint32_t>(interaction_profiles_.profile_count, MAX_INTERACTION_PROFILES);
    for (uint32_t index = 0; index < count; ++index) {
        interaction_profile_storage_.emplace_back(interaction_profiles_.profiles[index]);
    }
    return true;
}

}  // namespace ipc
}  // namespace ox