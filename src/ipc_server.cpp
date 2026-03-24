#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "messages.h"
#include "shared_memory.h"

#ifdef _WIN32
#define OX_IPC_BACKEND_EXPORT __declspec(dllexport)
#else
#define OX_IPC_BACKEND_EXPORT __attribute__((visibility("default")))
#endif

namespace ox {
namespace ipc {

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static OxDriverCallbacks g_driver_callbacks{};
static bool g_driver_set = false;
static bool g_running = false;
static nng_socket g_sock = NNG_SOCKET_INITIALIZER;
static bool g_socket_open = false;
static SharedData* g_shared_data = nullptr;
static uint64_t g_frame_counter = 0;
static std::mutex g_mutex;
static std::thread g_server_thread;
static std::thread g_frame_thread;
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

template <typename T>
void SendPayload(nng_socket sock, const MessageHeader& request, const T& payload) {
    size_t size = 0;
    void* buffer = BuildMessage(MessageType::RESPONSE, request.sequence, &payload, sizeof(T), &size);
    if (!buffer) {
        return;
    }

    const int rv = nng_send(sock, buffer, size, NNG_FLAG_ALLOC);
    if (rv != 0) {
        nng_free(buffer, size);
    }
}

void SendEmpty(nng_socket sock, uint32_t sequence) {
    size_t size = 0;
    void* buffer = BuildMessage(MessageType::RESPONSE, sequence, nullptr, 0, &size);
    if (!buffer) {
        return;
    }

    const int rv = nng_send(sock, buffer, size, NNG_FLAG_ALLOC);
    if (rv != 0) {
        nng_free(buffer, size);
    }
}

void SendEmpty(nng_socket sock, const MessageHeader& request) { SendEmpty(sock, request.sequence); }

int64_t NowNanos() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

void WritePose(PoseState& pose_state, const XrPosef& pose, uint64_t timestamp) {
    const uint32_t seq = pose_state.seq.load(std::memory_order_relaxed);
    pose_state.seq.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    pose_state.pose = pose;
    pose_state.timestamp = timestamp;
    std::atomic_thread_fence(std::memory_order_release);
    pose_state.seq.store(seq + 2, std::memory_order_release);
}

void WriteDeviceState(DeviceState& device_state, const OxDeviceState& source, uint64_t timestamp) {
    const uint32_t seq = device_state.seq.load(std::memory_order_relaxed);
    device_state.seq.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    std::snprintf(device_state.user_path, sizeof(device_state.user_path), "%s", source.user_path);
    device_state.pose = source.pose;
    device_state.timestamp = timestamp;
    device_state.is_active = source.is_active;
    std::atomic_thread_fence(std::memory_order_release);
    device_state.seq.store(seq + 2, std::memory_order_release);
}

void UpdateInputSlot(InputSlot& slot, const OxDriverCallbacks& callbacks, int64_t predicted_time) {
    switch (slot.type) {
        case InputSlotType::BOOLEAN: {
            if (!callbacks.get_input_state_boolean) {
                slot.is_available.store(0, std::memory_order_release);
                return;
            }
            XrBool32 value = XR_FALSE;
            const bool available = callbacks.get_input_state_boolean(predicted_time, slot.user_path,
                                                                     slot.component_path, &value) == XR_SUCCESS;
            slot.bool_value = value;
            slot.is_available.store(available ? 1u : 0u, std::memory_order_release);
            return;
        }
        case InputSlotType::FLOAT: {
            if (!callbacks.get_input_state_float) {
                slot.is_available.store(0, std::memory_order_release);
                return;
            }
            float value = 0.0f;
            const bool available = callbacks.get_input_state_float(predicted_time, slot.user_path, slot.component_path,
                                                                   &value) == XR_SUCCESS;
            slot.float_value = value;
            slot.is_available.store(available ? 1u : 0u, std::memory_order_release);
            return;
        }
        case InputSlotType::VECTOR2F: {
            if (!callbacks.get_input_state_vector2f) {
                slot.is_available.store(0, std::memory_order_release);
                return;
            }
            XrVector2f value{};
            const bool available = callbacks.get_input_state_vector2f(predicted_time, slot.user_path,
                                                                      slot.component_path, &value) == XR_SUCCESS;
            slot.vec2f_value = value;
            slot.is_available.store(available ? 1u : 0u, std::memory_order_release);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Internal (file-scope) functions
// ---------------------------------------------------------------------------

static void InitializeSharedData() {
    std::memset(g_shared_data, 0, sizeof(SharedData));
    g_shared_data->protocol_version.store(PROTOCOL_VERSION, std::memory_order_release);
    g_shared_data->backend_ready.store(1, std::memory_order_release);
    g_shared_data->frontend_connected.store(0, std::memory_order_release);
}

static void HandleConnect(const MessageHeader& request) { SendEmpty(g_sock, request); }

static void HandleDisconnect(const MessageHeader& request) { SendEmpty(g_sock, request); }

static void HandleMetadataRequest(const MessageHeader& request, MessageType type) {
    switch (type) {
        case MessageType::GET_SYSTEM_PROPERTIES: {
            XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
            g_driver_callbacks.get_system_properties(&props);
            SendPayload(g_sock, request, props);
            break;
        }
        case MessageType::GET_INTERACTION_PROFILES: {
            InteractionProfilesResponse response{};
            if (g_driver_callbacks.get_interaction_profiles) {
                const char* profiles[MAX_INTERACTION_PROFILES] = {};
                const uint32_t total = g_driver_callbacks.get_interaction_profiles(profiles, MAX_INTERACTION_PROFILES);
                response.profile_count = std::min<uint32_t>(total, MAX_INTERACTION_PROFILES);
                for (uint32_t index = 0; index < response.profile_count; ++index) {
                    std::snprintf(response.profiles[index], sizeof(response.profiles[index]), "%s", profiles[index]);
                }
            } else {
                response.profile_count = 1;
                std::snprintf(response.profiles[0], sizeof(response.profiles[0]), "%s",
                              "/interaction_profiles/khr/simple_controller");
            }
            SendPayload(g_sock, request, response);
            break;
        }
        default:
            SendEmpty(g_sock, request);
            break;
    }
}

static void HandleRegisterInput(const MessageHeader& request, const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(RegisterInputRequest) || !g_shared_data) {
        SendPayload(g_sock, request, RegisterInputResponse{MAX_INPUT_SLOTS});
        return;
    }

    const auto* input_request = reinterpret_cast<const RegisterInputRequest*>(payload.data());
    auto& table = g_shared_data->input_state;
    const uint32_t count = table.slot_count.load(std::memory_order_acquire);

    for (uint32_t index = 0; index < count; ++index) {
        const auto& slot = table.slots[index];
        if (std::strncmp(slot.user_path, input_request->user_path, sizeof(slot.user_path)) == 0 &&
            std::strncmp(slot.component_path, input_request->component_path, sizeof(slot.component_path)) == 0 &&
            slot.type == input_request->type) {
            SendPayload(g_sock, request, RegisterInputResponse{index});
            return;
        }
    }

    if (count >= MAX_INPUT_SLOTS) {
        spdlog::warn("IpcServer: MAX_INPUT_SLOTS ({}) reached", MAX_INPUT_SLOTS);
        SendPayload(g_sock, request, RegisterInputResponse{MAX_INPUT_SLOTS});
        return;
    }

    auto& slot = table.slots[count];
    std::memset(&slot, 0, sizeof(slot));
    std::snprintf(slot.user_path, sizeof(slot.user_path), "%s", input_request->user_path);
    std::snprintf(slot.component_path, sizeof(slot.component_path), "%s", input_request->component_path);
    slot.type = input_request->type;
    slot.is_available.store(0, std::memory_order_relaxed);
    UpdateInputSlot(slot, g_driver_callbacks, NowNanos());
    table.slot_count.store(count + 1, std::memory_order_release);
    SendPayload(g_sock, request, RegisterInputResponse{count});
}

static void HandleSessionState(const MessageHeader& request, const std::vector<uint8_t>& payload) {
    if (payload.size() >= sizeof(SessionStateNotification) && g_driver_callbacks.on_session_state_changed) {
        const auto* notification = reinterpret_cast<const SessionStateNotification*>(payload.data());
        g_driver_callbacks.on_session_state_changed(notification->state);
    }
    SendEmpty(g_sock, request);
}

static void ServerLoop() {
    while (g_running) {
        void* buffer = nullptr;
        size_t size = 0;
        const int rv = nng_recv(g_sock, &buffer, &size, NNG_FLAG_ALLOC);
        if (rv != 0) {
            if (g_running) {
                spdlog::warn("ServerLoop nng_recv failed: {}", nng_strerror(rv));
            }
            continue;
        }

        if (size < sizeof(MessageHeader)) {
            nng_free(buffer, size);
            SendEmpty(g_sock, 0);
            continue;
        }

        MessageHeader header{};
        std::memcpy(&header, buffer, sizeof(MessageHeader));

        std::vector<uint8_t> payload;
        if (header.payload_size > 0 && size >= sizeof(MessageHeader) + header.payload_size) {
            const auto* payload_begin = static_cast<uint8_t*>(buffer) + sizeof(MessageHeader);
            payload.assign(payload_begin, payload_begin + header.payload_size);
        }

        nng_free(buffer, size);

        switch (header.type) {
            case MessageType::CONNECT:
                if (g_shared_data) {
                    g_shared_data->frontend_connected.store(1, std::memory_order_release);
                }
                HandleConnect(header);
                break;
            case MessageType::DISCONNECT:
                if (g_shared_data) {
                    g_shared_data->frontend_connected.store(0, std::memory_order_release);
                }
                HandleDisconnect(header);
                break;
            case MessageType::GET_SYSTEM_PROPERTIES:
            case MessageType::GET_INTERACTION_PROFILES:
                HandleMetadataRequest(header, header.type);
                break;
            case MessageType::REGISTER_INPUT:
                HandleRegisterInput(header, payload);
                break;
            case MessageType::NOTIFY_SESSION_STATE:
                HandleSessionState(header, payload);
                break;
            default:
                SendEmpty(g_sock, header);
                break;
        }
    }
}

static void FrameLoop() {
    using namespace std::chrono;
    auto next_frame = steady_clock::now();
    const auto frame_interval = duration_cast<steady_clock::duration>(duration<double>(1.0 / 90.0));

    while (g_running) {
        next_frame += frame_interval;

        if (g_shared_data) {
            auto& frame = g_shared_data->frame_state;
            const int64_t predicted_time = NowNanos();
            frame.frame_id.store(g_frame_counter++, std::memory_order_release);
            frame.predicted_display_time.store(predicted_time, std::memory_order_release);
            frame.view_count.store(2, std::memory_order_release);

            for (uint32_t eye_index = 0; eye_index < 2; ++eye_index) {
                XrView view{XR_TYPE_VIEW};
                g_driver_callbacks.update_view(predicted_time, eye_index, &view);
                auto& shared_view = frame.views[eye_index];
                WritePose(shared_view.pose, view.pose, static_cast<uint64_t>(predicted_time));
                shared_view.fov = view.fov;
            }

            uint32_t device_count = 0;
            OxDeviceState devices[OX_MAX_DEVICES]{};
            if (g_driver_callbacks.update_devices) {
                g_driver_callbacks.update_devices(predicted_time, devices, &device_count);
            }

            device_count = std::min<uint32_t>(device_count, OX_MAX_DEVICES);
            frame.device_count.store(device_count, std::memory_order_release);
            for (uint32_t index = 0; index < device_count; ++index) {
                WriteDeviceState(frame.device_states[index], devices[index], static_cast<uint64_t>(predicted_time));
            }

            auto& input_state = g_shared_data->input_state;
            const uint32_t slot_count = input_state.slot_count.load(std::memory_order_acquire);
            for (uint32_t index = 0; index < slot_count; ++index) {
                UpdateInputSlot(input_state.slots[index], g_driver_callbacks, predicted_time);
            }

            if (g_driver_callbacks.submit_frame_pixels) {
                for (uint32_t eye_index = 0; eye_index < 2; ++eye_index) {
                    auto& texture = frame.textures[eye_index];
                    if (texture.ready.load(std::memory_order_acquire) == 1) {
                        texture.ready.store(0, std::memory_order_release);
                        g_driver_callbacks.submit_frame_pixels(
                            predicted_time, eye_index, texture.width.load(std::memory_order_relaxed),
                            texture.height.load(std::memory_order_relaxed),
                            texture.format.load(std::memory_order_relaxed), texture.pixel_data,
                            texture.data_size.load(std::memory_order_relaxed));
                    }
                }
            }
        }

        std::this_thread::sleep_until(next_frame);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SetDriver(const OxDriverCallbacks* callbacks) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_driver_callbacks = callbacks ? *callbacks : OxDriverCallbacks{};
    g_driver_set = callbacks != nullptr;
}

bool Initialize() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_running) {
        return true;
    }

    if (!g_driver_set || !g_driver_callbacks.initialize || !g_driver_callbacks.is_device_connected ||
        !g_driver_callbacks.get_system_properties || !g_driver_callbacks.update_view) {
        spdlog::error("IPC backend driver is not configured correctly");
        return false;
    }

    if (!g_driver_callbacks.is_device_connected()) {
        spdlog::error("IPC backend driver reported no device");
        return false;
    }

    if (!g_shared_memory.Create(SHARED_MEMORY_NAME, sizeof(SharedData), true)) {
        spdlog::error("Failed to create IPC shared memory");
        return false;
    }

    g_shared_data = static_cast<SharedData*>(g_shared_memory.GetPointer());
    if (!g_shared_data) {
        g_shared_memory.Close();
        return false;
    }

    InitializeSharedData();

    int rv = nng_pair0_open(&g_sock);
    if (rv != 0) {
        spdlog::error("Failed to open nng socket: {}", nng_strerror(rv));
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    g_socket_open = true;
    rv = nng_listen(g_sock, CONTROL_CHANNEL_URL, nullptr, 0);
    if (rv != 0) {
        spdlog::error("Failed to listen on {}: {}", CONTROL_CHANNEL_URL, nng_strerror(rv));
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_socket_open = false;
        g_shared_memory.Close();
        g_shared_data = nullptr;
        return false;
    }

    g_running = true;
    g_server_thread = std::thread(ServerLoop);
    g_frame_thread = std::thread(FrameLoop);
    return true;
}

void Shutdown() {
    g_running = false;

    if (g_socket_open) {
        nng_close(g_sock);
        g_sock = NNG_SOCKET_INITIALIZER;
        g_socket_open = false;
    }

    if (g_server_thread.joinable()) {
        g_server_thread.join();
    }
    if (g_frame_thread.joinable()) {
        g_frame_thread.join();
    }

    g_shared_memory.Close();
    protocol::UnlinkSharedMemory(SHARED_MEMORY_NAME);
    g_shared_data = nullptr;
}

}  // namespace ipc
}  // namespace ox

extern "C" {

OX_IPC_BACKEND_EXPORT void ox_ipc_backend_set_driver(const OxDriverCallbacks* callbacks) {
    ox::ipc::SetDriver(callbacks);
}

OX_IPC_BACKEND_EXPORT int ox_ipc_backend_initialize() { return ox::ipc::Initialize() ? 1 : 0; }

OX_IPC_BACKEND_EXPORT void ox_ipc_backend_shutdown() { ox::ipc::Shutdown(); }
}
