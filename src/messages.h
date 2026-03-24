#pragma once

#include <ox_driver.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ox {
namespace ipc {

static constexpr uint32_t PROTOCOL_VERSION = 1;
static constexpr const char* SHARED_MEMORY_NAME = "ox_ipc_proxy_shm";
static constexpr uint32_t MAX_INTERACTION_PROFILES = 16;
constexpr uint32_t MAX_INPUT_SLOTS = 64;

#ifdef _WIN32
static constexpr const char* CONTROL_CHANNEL_URL = "ipc://ox_ipc_proxy_control";
#else
static constexpr const char* CONTROL_CHANNEL_URL = "ipc:///tmp/ox_ipc_proxy_control.ipc";
#endif

enum class MessageType : uint32_t {
    CONNECT = 1,
    DISCONNECT = 2,
    GET_SYSTEM_PROPERTIES = 3,
    GET_INTERACTION_PROFILES = 6,
    NOTIFY_SESSION_STATE = 10,
    REGISTER_INPUT = 11,
    RESPONSE = 100,
};

enum class InputSlotType : uint32_t {
    BOOLEAN = 0,
    FLOAT = 1,
    VECTOR2F = 2,
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

struct RegisterInputRequest {
    char user_path[256];
    char component_path[128];
    InputSlotType type;
    uint32_t padding;
};

struct RegisterInputResponse {
    uint32_t slot_index;
};

struct SessionStateNotification {
    XrSessionState state;
};

struct alignas(64) PoseState {
    std::atomic<uint32_t> seq;
    uint32_t padding0[15];
    XrPosef pose;
    uint64_t timestamp;
    uint32_t padding1[6];
};

struct alignas(64) DeviceState {
    std::atomic<uint32_t> seq;
    uint32_t padding0[15];
    char user_path[256];
    XrPosef pose;
    uint64_t timestamp;
    uint32_t is_active;
    uint32_t padding1[7];
};

struct ViewState {
    PoseState pose;
    XrFovf fov;
};

struct alignas(64) InputSlot {
    char user_path[256];
    char component_path[128];
    InputSlotType type;
    std::atomic<uint32_t> is_available;
    union {
        uint32_t bool_value;
        float float_value;
        XrVector2f vec2f_value;
    };
    uint32_t padding[2];
};

struct alignas(64) InputStateTable {
    std::atomic<uint32_t> slot_count;
    uint32_t padding[15];
    InputSlot slots[MAX_INPUT_SLOTS];
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
    InputStateTable input_state;
};

static_assert(alignof(SharedData) == 4096, "SharedData alignment is not 4096 bytes");

}  // namespace ipc
}  // namespace ox
