#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "messages.h"
#include "shared_memory.h"

namespace ox {
namespace ipc {

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static nng_socket g_sock = NNG_SOCKET_INITIALIZER;
static SharedData* g_shared_data = nullptr;
static bool g_connected = false;
static uint32_t g_sequence = 1;
static XrSystemProperties g_system_properties{XR_TYPE_SYSTEM_PROPERTIES};
static InteractionProfilesResponse g_interaction_profiles{};
static std::vector<std::string> g_interaction_profile_storage;
static std::map<std::string, uint32_t> g_input_slot_cache;
static std::mutex g_mutex;
static protocol::SharedMemory g_shared_memory;

namespace {

void* BuildMessage(MessageType type, uint32_t sequence, const void* payload, uint32_t payload_size, size_t* out_size) {
    const size_t total_size = sizeof(MessageHeader) + payload_size;
    void* buffer = nng_alloc(total_size);
    if (!buffer) {
        return nullptr;
    }

    auto* header = static_cast<MessageHeader*>(buffer);
    header->type = type;
    header->sequence = sequence;
    header->payload_size = payload_size;
    header->reserved = 0;

    if (payload && payload_size > 0) {
        std::memcpy(static_cast<uint8_t*>(buffer) + sizeof(MessageHeader), payload, payload_size);
    }

    *out_size = total_size;
    return buffer;
}

XrPosef ReadPose(const PoseState& pose_state) {
    XrPosef pose{};
    while (true) {
        const uint32_t seq0 = pose_state.seq.load(std::memory_order_acquire);
        if ((seq0 & 1u) != 0u) {
            continue;
        }

        std::atomic_thread_fence(std::memory_order_acquire);
        pose = pose_state.pose;
        std::atomic_thread_fence(std::memory_order_acquire);

        const uint32_t seq1 = pose_state.seq.load(std::memory_order_acquire);
        if (seq0 == seq1) {
            return pose;
        }
    }
}

struct DeviceSnapshot {
    char user_path[256];
    XrPosef pose;
    uint64_t timestamp;
    XrBool32 is_active;
};

DeviceSnapshot ReadDeviceState(const DeviceState& device_state) {
    DeviceSnapshot snapshot{};
    while (true) {
        const uint32_t seq0 = device_state.seq.load(std::memory_order_acquire);
        if ((seq0 & 1u) != 0u) {
            continue;
        }

        std::atomic_thread_fence(std::memory_order_acquire);
        std::memcpy(snapshot.user_path, device_state.user_path, sizeof(snapshot.user_path));
        snapshot.pose = device_state.pose;
        snapshot.timestamp = device_state.timestamp;
        snapshot.is_active = device_state.is_active;
        std::atomic_thread_fence(std::memory_order_acquire);

        const uint32_t seq1 = device_state.seq.load(std::memory_order_acquire);
        if (seq0 == seq1) {
            return snapshot;
        }
    }
}

std::string MakeInputKey(const char* user_path, const char* component_path, InputSlotType type) {
    return std::to_string(static_cast<uint32_t>(type)) + "|" + user_path + "|" + component_path;
}

}  // namespace

// ---------------------------------------------------------------------------
// Internal helper (needs access to globals, so outside anonymous namespace)
// ---------------------------------------------------------------------------

template <typename ResponseType>
static bool SendRequestLocked(MessageType type, const void* payload, uint32_t payload_size, ResponseType* response) {
    if (!g_connected) {
        return false;
    }

    const uint32_t sequence = g_sequence++;
    size_t size = 0;
    void* buffer = BuildMessage(type, sequence, payload, payload_size, &size);
    if (!buffer) {
        return false;
    }

    int rv = nng_send(g_sock, buffer, size, NNG_FLAG_ALLOC);
    if (rv != 0) {
        nng_free(buffer, size);
        return false;
    }

    void* reply_buffer = nullptr;
    size_t reply_size = 0;
    rv = nng_recv(g_sock, &reply_buffer, &reply_size, NNG_FLAG_ALLOC);
    if (rv != 0) {
        return false;
    }

    bool ok = false;
    if (reply_size >= sizeof(MessageHeader)) {
        MessageHeader reply{};
        std::memcpy(&reply, reply_buffer, sizeof(MessageHeader));
        if (reply.type == MessageType::RESPONSE && reply.sequence == sequence) {
            if constexpr (std::is_same_v<ResponseType, MessageHeader>) {
                ok = true;
            } else if (response && reply_size >= sizeof(MessageHeader) + sizeof(ResponseType)) {
                std::memcpy(response, static_cast<uint8_t*>(reply_buffer) + sizeof(MessageHeader),
                            sizeof(ResponseType));
                ok = true;
            }
        }
    }

    nng_free(reply_buffer, reply_size);
    return ok;
}

static bool QueryMetadata() {
    if (!SendRequestLocked(MessageType::GET_SYSTEM_PROPERTIES, nullptr, 0, &g_system_properties)) {
        return false;
    }
    if (!SendRequestLocked(MessageType::GET_INTERACTION_PROFILES, nullptr, 0, &g_interaction_profiles)) {
        return false;
    }

    g_interaction_profile_storage.clear();
    const uint32_t count = std::min<uint32_t>(g_interaction_profiles.profile_count, MAX_INTERACTION_PROFILES);
    for (uint32_t index = 0; index < count; ++index) {
        g_interaction_profile_storage.emplace_back(g_interaction_profiles.profiles[index]);
    }
    return true;
}

static uint32_t EnsureInputRegistered(const char* user_path, const char* component_path, InputSlotType type) {
    const std::string key = MakeInputKey(user_path, component_path, type);
    const auto found = g_input_slot_cache.find(key);
    if (found != g_input_slot_cache.end()) {
        return found->second;
    }

    RegisterInputRequest request{};
    std::snprintf(request.user_path, sizeof(request.user_path), "%s", user_path);
    std::snprintf(request.component_path, sizeof(request.component_path), "%s", component_path);
    request.type = type;

    RegisterInputResponse response{MAX_INPUT_SLOTS};
    if (SendRequestLocked(MessageType::REGISTER_INPUT, &request, sizeof(request), &response) &&
        response.slot_index < MAX_INPUT_SLOTS) {
        g_input_slot_cache[key] = response.slot_index;
        return response.slot_index;
    }

    return MAX_INPUT_SLOTS;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static bool Connect() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_connected) {
        return true;
    }

    if (!g_shared_memory.Create(SHARED_MEMORY_NAME, sizeof(SharedData), false)) {
        spdlog::error("Failed to open IPC shared memory");
        return false;
    }

    g_shared_data = static_cast<SharedData*>(g_shared_memory.GetPointer());
    if (!g_shared_data) {
        spdlog::error("IPC shared memory pointer is null");
        g_shared_memory.Close();
        return false;
    }

    if (g_shared_data->protocol_version.load(std::memory_order_acquire) != PROTOCOL_VERSION) {
        spdlog::error("IPC protocol version mismatch");
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    int rv = nng_pair0_open(&g_sock);
    if (rv != 0) {
        spdlog::error("Failed to open nng socket: {}", nng_strerror(rv));
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    rv = nng_socket_set_ms(g_sock, NNG_OPT_SENDTIMEO, 5000);
    if (rv != 0) {
        spdlog::error("Failed to set nng send timeout: {}", nng_strerror(rv));
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    rv = nng_socket_set_ms(g_sock, NNG_OPT_RECVTIMEO, 5000);
    if (rv != 0) {
        spdlog::error("Failed to set nng recv timeout: {}", nng_strerror(rv));
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    rv = nng_dial(g_sock, CONTROL_CHANNEL_URL, nullptr, 0);
    if (rv != 0) {
        spdlog::error("Failed to connect to IPC control channel {}: {}", CONTROL_CHANNEL_URL, nng_strerror(rv));
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    g_connected = true;
    if (!SendRequestLocked<MessageHeader>(MessageType::CONNECT, nullptr, 0, nullptr)) {
        spdlog::error("Failed to complete IPC connect request");
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_shared_memory.Close();
        g_shared_data = nullptr;
        g_connected = false;
        return false;
    }

    if (!QueryMetadata()) {
        spdlog::error("Failed to query IPC metadata");
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_shared_memory.Close();
        g_shared_data = nullptr;
        g_connected = false;
        return false;
    }

    g_shared_data->frontend_connected.store(1, std::memory_order_release);
    return true;
}

static void Disconnect() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_connected) {
        size_t size = 0;
        void* buffer = BuildMessage(MessageType::DISCONNECT, g_sequence++, nullptr, 0, &size);
        if (buffer) {
            const int rv = nng_send(g_sock, buffer, size, NNG_FLAG_ALLOC);
            if (rv != 0) {
                nng_free(buffer, size);
            }
        }
    }

    if (g_shared_data) {
        g_shared_data->frontend_connected.store(0, std::memory_order_release);
    }

    if (g_connected) {
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
    }

    g_shared_memory.Close();
    g_shared_data = nullptr;
    g_connected = false;
    g_sequence = 1;
    g_input_slot_cache.clear();
    g_interaction_profile_storage.clear();
}

static int IsConnected() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_connected ? 1 : 0;
}

static void GetSystemProperties(XrSystemProperties* props) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (props) {
        void* next = props->next;
        *props = g_system_properties;
        props->next = next;
    }
}

static void UpdateView(XrTime predicted_time, uint32_t eye_index, XrView* out_view) {
    std::lock_guard<std::mutex> lock(g_mutex);
    (void)predicted_time;
    if (!g_connected || !g_shared_data || !out_view || eye_index >= 2) {
        return;
    }
    out_view->pose = ReadPose(g_shared_data->frame_state.views[eye_index].pose);
    out_view->fov = g_shared_data->frame_state.views[eye_index].fov;
}

static void UpdateDevices(XrTime predicted_time, OxDeviceState* out_states, uint32_t* out_count) {
    std::lock_guard<std::mutex> lock(g_mutex);
    (void)predicted_time;
    if (!g_connected || !g_shared_data || !out_count) {
        if (out_count) {
            *out_count = 0;
        }
        return;
    }

    const uint32_t count =
        std::min<uint32_t>(g_shared_data->frame_state.device_count.load(std::memory_order_acquire), OX_MAX_DEVICES);
    *out_count = count;
    if (!out_states) {
        return;
    }

    for (uint32_t index = 0; index < count; ++index) {
        const DeviceSnapshot snapshot = ReadDeviceState(g_shared_data->frame_state.device_states[index]);
        std::snprintf(out_states[index].user_path, sizeof(out_states[index].user_path), "%s", snapshot.user_path);
        out_states[index].pose = snapshot.pose;
        out_states[index].is_active = snapshot.is_active;
    }
}

static XrResult GetInputStateBoolean(XrTime predicted_time, const char* user_path, const char* component_path,
                                     XrBool32* out_value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    (void)predicted_time;
    if (!g_connected || !g_shared_data) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    const uint32_t index = EnsureInputRegistered(user_path, component_path, InputSlotType::BOOLEAN);
    if (index >= MAX_INPUT_SLOTS) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    const auto& slot = g_shared_data->input_state.slots[index];
    const bool available = slot.is_available.load(std::memory_order_acquire) != 0;
    if (out_value && available) {
        *out_value = slot.bool_value ? XR_TRUE : XR_FALSE;
    }
    return available ? XR_SUCCESS : XR_ERROR_PATH_UNSUPPORTED;
}

static XrResult GetInputStateFloat(XrTime predicted_time, const char* user_path, const char* component_path,
                                   float* out_value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    (void)predicted_time;
    if (!g_connected || !g_shared_data) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    const uint32_t index = EnsureInputRegistered(user_path, component_path, InputSlotType::FLOAT);
    if (index >= MAX_INPUT_SLOTS) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    const auto& slot = g_shared_data->input_state.slots[index];
    const bool available = slot.is_available.load(std::memory_order_acquire) != 0;
    if (out_value && available) {
        *out_value = slot.float_value;
    }
    return available ? XR_SUCCESS : XR_ERROR_PATH_UNSUPPORTED;
}

static XrResult GetInputStateVector2f(XrTime predicted_time, const char* user_path, const char* component_path,
                                      XrVector2f* out_value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    (void)predicted_time;
    if (!g_connected || !g_shared_data) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    const uint32_t index = EnsureInputRegistered(user_path, component_path, InputSlotType::VECTOR2F);
    if (index >= MAX_INPUT_SLOTS) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    const auto& slot = g_shared_data->input_state.slots[index];
    const bool available = slot.is_available.load(std::memory_order_acquire) != 0;
    if (out_value && available) {
        *out_value = slot.vec2f_value;
    }
    return available ? XR_SUCCESS : XR_ERROR_PATH_UNSUPPORTED;
}

static uint32_t GetInteractionProfiles(const char** profiles, uint32_t max_profiles) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t count =
        std::min<uint32_t>(static_cast<uint32_t>(g_interaction_profile_storage.size()), max_profiles);
    if (profiles) {
        for (uint32_t index = 0; index < count; ++index) {
            profiles[index] = g_interaction_profile_storage[index].c_str();
        }
    }
    return static_cast<uint32_t>(g_interaction_profile_storage.size());
}

static void NotifySessionState(XrSessionState state) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_connected) {
        return;
    }
    SessionStateNotification notification{state};
    SendRequestLocked<MessageHeader>(MessageType::NOTIFY_SESSION_STATE, &notification, sizeof(notification), nullptr);
}

static void SubmitFramePixels(XrTime frame_time, uint32_t eye_index, uint32_t width, uint32_t height, uint32_t format,
                              const void* pixel_data, uint32_t data_size) {
    std::lock_guard<std::mutex> lock(g_mutex);
    (void)frame_time;
    if (!g_connected || !g_shared_data || !pixel_data || eye_index >= 2 || data_size > MAX_TEXTURE_SIZE) {
        return;
    }
    auto& texture = g_shared_data->frame_state.textures[eye_index];
    texture.ready.store(0, std::memory_order_release);
    texture.width.store(width, std::memory_order_relaxed);
    texture.height.store(height, std::memory_order_relaxed);
    texture.format.store(format, std::memory_order_relaxed);
    texture.data_size.store(data_size, std::memory_order_relaxed);
    std::memcpy(texture.pixel_data, pixel_data, data_size);
    texture.ready.store(1, std::memory_order_release);
}

}  // namespace ipc
}  // namespace ox

extern "C" OX_DRIVER_EXPORT int ox_driver_register(OxDriverCallbacks* callbacks) {
    if (!callbacks) {
        return 0;
    }

    *callbacks = {};
    callbacks->initialize = []() -> int { return ox::ipc::Connect() ? 1 : 0; };
    callbacks->shutdown = ox::ipc::Disconnect;
    callbacks->is_device_connected = ox::ipc::IsConnected;
    callbacks->get_system_properties = ox::ipc::GetSystemProperties;
    callbacks->update_view = ox::ipc::UpdateView;
    callbacks->update_devices = ox::ipc::UpdateDevices;
    callbacks->get_input_state_boolean = ox::ipc::GetInputStateBoolean;
    callbacks->get_input_state_float = ox::ipc::GetInputStateFloat;
    callbacks->get_input_state_vector2f = ox::ipc::GetInputStateVector2f;
    callbacks->get_interaction_profiles = ox::ipc::GetInteractionProfiles;
    callbacks->on_session_state_changed = ox::ipc::NotifySessionState;
    callbacks->submit_frame_pixels = ox::ipc::SubmitFramePixels;
    return 1;
}
