#pragma once

#include <ox_driver.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ox {
namespace ipc {

static constexpr uint32_t PROTOCOL_VERSION = 1;
static constexpr const char* SHARED_MEMORY_NAME = "ox_ipc_proxy_shm";
static constexpr const char* CONTROL_CHANNEL_NAME = "ox_ipc_proxy_control";
static constexpr uint32_t MAX_INTERACTION_PROFILES = 8;

enum class MessageType : uint32_t {
    CONNECT = 1,
    DISCONNECT = 2,
    GET_DEVICE_INFO = 3,
    GET_DISPLAY_PROPERTIES = 4,
    GET_TRACKING_CAPABILITIES = 5,
    GET_INTERACTION_PROFILES = 6,
    GET_INPUT_STATE_BOOLEAN = 7,
    GET_INPUT_STATE_FLOAT = 8,
    GET_INPUT_STATE_VECTOR2F = 9,
    NOTIFY_SESSION_STATE = 10,
    RESPONSE = 100,
};

struct MessageHeader {
    MessageType type;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t reserved;
};

struct InteractionProfilesResponse {
    uint32_t profile_count;
    char profiles[MAX_INTERACTION_PROFILES][128];
};

struct InputStateRequest {
    char user_path[256];
    char component_path[128];
    int64_t predicted_time;
};

struct InputStateBooleanResponse {
    uint32_t is_available;
    uint32_t value;
};

struct InputStateFloatResponse {
    uint32_t is_available;
    float value;
};

struct InputStateVector2fResponse {
    uint32_t is_available;
    OxVector2f value;
};

struct SessionStateNotification {
    OxSessionState state;
};

struct alignas(64) PoseState {
    OxPose pose;
    uint64_t timestamp;
    std::atomic<uint32_t> flags;
    uint32_t padding[3];
};

struct DeviceState {
    char user_path[256];
    PoseState pose;
    uint32_t is_active;
    uint32_t padding;
};

struct ViewState {
    PoseState pose;
    OxFov fov;
};

constexpr uint32_t MAX_TEXTURE_WIDTH = 2048;
constexpr uint32_t MAX_TEXTURE_HEIGHT = 2048;
constexpr uint32_t MAX_TEXTURE_CHANNELS = 4;
constexpr uint32_t MAX_TEXTURE_SIZE = MAX_TEXTURE_WIDTH * MAX_TEXTURE_HEIGHT * MAX_TEXTURE_CHANNELS;

struct FrameTexture {
    std::atomic<uint32_t> width;
    std::atomic<uint32_t> height;
    std::atomic<uint32_t> format;
    std::atomic<uint32_t> data_size;
    std::atomic<uint32_t> ready;
    uint32_t padding[3];
    std::byte pixel_data[MAX_TEXTURE_SIZE];
};

struct alignas(64) FrameState {
    std::atomic<uint64_t> frame_id;
    std::atomic<uint64_t> predicted_display_time;
    std::atomic<uint32_t> view_count;
    std::atomic<uint32_t> flags;
    ViewState views[2];
    std::atomic<uint32_t> device_count;
    uint32_t padding1;
    DeviceState device_states[OX_MAX_DEVICES];
    FrameTexture textures[2];
};

struct alignas(4096) SharedData {
    std::atomic<uint32_t> protocol_version;
    std::atomic<uint32_t> backend_ready;
    std::atomic<uint32_t> frontend_connected;
    uint32_t padding1;
    FrameState frame_state;
};

static_assert(alignof(SharedData) == 4096, "SharedData alignment is not 4096 bytes");

}  // namespace ipc
}  // namespace ox