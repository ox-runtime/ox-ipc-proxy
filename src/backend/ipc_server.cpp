#include "ipc_server.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ox {
namespace ipc {

namespace {

template <typename T>
void SendPayload(protocol::ControlChannel& channel, const MessageHeader& request, const T& payload) {
    MessageHeader response{};
    response.type = MessageType::RESPONSE;
    response.sequence = request.sequence;
    response.payload_size = sizeof(T);
    channel.Send(response, &payload);
}

void SendEmpty(protocol::ControlChannel& channel, const MessageHeader& request) {
    MessageHeader response{};
    response.type = MessageType::RESPONSE;
    response.sequence = request.sequence;
    response.payload_size = 0;
    channel.Send(response);
}

int64_t NowNanos() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

}  // namespace

IpcServer& IpcServer::Instance() {
    static IpcServer instance;
    return instance;
}

IpcServer::IpcServer()
    : driver_callbacks_{}, driver_set_(false), running_(false), shared_data_(nullptr), frame_counter_(0) {}

IpcServer::~IpcServer() { Shutdown(); }

void IpcServer::SetDriver(const OxDriverCallbacks* callbacks) {
    std::lock_guard<std::mutex> lock(mutex_);
    driver_callbacks_ = callbacks ? *callbacks : OxDriverCallbacks{};
    driver_set_ = callbacks != nullptr;
}

bool IpcServer::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }

    if (!driver_set_ || !driver_callbacks_.initialize || !driver_callbacks_.is_device_connected ||
        !driver_callbacks_.get_device_info || !driver_callbacks_.get_display_properties ||
        !driver_callbacks_.get_tracking_capabilities || !driver_callbacks_.update_view_pose) {
        spdlog::error("IPC backend driver is not configured correctly");
        return false;
    }

    if (!driver_callbacks_.is_device_connected()) {
        spdlog::error("IPC backend driver reported no device");
        return false;
    }

    if (!shared_memory_.Create(SHARED_MEMORY_NAME, sizeof(SharedData), true)) {
        spdlog::error("Failed to create IPC shared memory");
        return false;
    }

    shared_data_ = static_cast<SharedData*>(shared_memory_.GetPointer());
    if (!shared_data_) {
        shared_memory_.Close();
        return false;
    }

    InitializeSharedData();
    if (!control_channel_.CreateServer(CONTROL_CHANNEL_NAME)) {
        spdlog::error("Failed to create IPC control channel server");
        shared_memory_.Close();
        shared_data_ = nullptr;
        return false;
    }

    running_ = true;
    server_thread_ = std::thread([this]() { ServerLoop(); });
    frame_thread_ = std::thread([this]() { FrameLoop(); });
    return true;
}

void IpcServer::Shutdown() {
    running_ = false;

#ifdef _WIN32
    if (server_thread_.joinable()) {
        CancelSynchronousIo(server_thread_.native_handle());
    }
#endif

    control_channel_.Close();

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    if (frame_thread_.joinable()) {
        frame_thread_.join();
    }

    shared_memory_.Close();
    protocol::UnlinkSharedMemory(SHARED_MEMORY_NAME);
    shared_data_ = nullptr;
}

void IpcServer::ServerLoop() {
    while (running_) {
        if (!control_channel_.Accept()) {
            if (running_) {
                spdlog::error("IPC backend failed to accept connection");
            }
            return;
        }

        if (shared_data_) {
            shared_data_->frontend_connected.store(1, std::memory_order_release);
        }

        MessageLoop();

        if (shared_data_) {
            shared_data_->frontend_connected.store(0, std::memory_order_release);
        }

        if (running_) {
            control_channel_.Close();
            control_channel_.CreateServer(CONTROL_CHANNEL_NAME);
        }
    }
}

void IpcServer::MessageLoop() {
    MessageHeader header{};
    std::vector<uint8_t> payload;
    while (running_) {
        if (!control_channel_.Receive(header, payload)) {
            return;
        }

        switch (header.type) {
            case MessageType::CONNECT:
                HandleConnect(header);
                break;
            case MessageType::DISCONNECT:
                return;
            case MessageType::GET_DEVICE_INFO:
            case MessageType::GET_DISPLAY_PROPERTIES:
            case MessageType::GET_TRACKING_CAPABILITIES:
            case MessageType::GET_INTERACTION_PROFILES:
                HandleMetadataRequest(header, header.type);
                break;
            case MessageType::GET_INPUT_STATE_BOOLEAN:
            case MessageType::GET_INPUT_STATE_FLOAT:
            case MessageType::GET_INPUT_STATE_VECTOR2F:
                HandleInputRequest(header, payload);
                break;
            case MessageType::NOTIFY_SESSION_STATE:
                HandleSessionState(header, payload);
                break;
            default:
                SendEmpty(control_channel_, header);
                break;
        }
    }
}

void IpcServer::FrameLoop() {
    using namespace std::chrono;
    auto next_frame = steady_clock::now();
    const auto frame_interval = duration_cast<steady_clock::duration>(duration<double>(1.0 / 90.0));

    while (running_) {
        next_frame += frame_interval;

        if (shared_data_) {
            auto& frame = shared_data_->frame_state;
            const int64_t predicted_time = NowNanos();
            frame.frame_id.store(frame_counter_++, std::memory_order_release);
            frame.predicted_display_time.store(predicted_time, std::memory_order_release);
            frame.view_count.store(2, std::memory_order_release);

            OxDisplayProperties display_props{};
            driver_callbacks_.get_display_properties(&display_props);

            for (uint32_t eye_index = 0; eye_index < 2; ++eye_index) {
                OxPose pose{};
                driver_callbacks_.update_view_pose(predicted_time, eye_index, &pose);
                auto& view = frame.views[eye_index];
                view.pose.pose = pose;
                view.pose.timestamp = static_cast<uint64_t>(predicted_time);
                view.fov = display_props.fov;
            }

            uint32_t device_count = 0;
            OxDeviceState devices[OX_MAX_DEVICES] = {};
            if (driver_callbacks_.update_devices) {
                driver_callbacks_.update_devices(predicted_time, devices, &device_count);
            }

            device_count = std::min<uint32_t>(device_count, OX_MAX_DEVICES);
            frame.device_count.store(device_count, std::memory_order_release);
            for (uint32_t index = 0; index < device_count; ++index) {
                auto& dest = frame.device_states[index];
                snprintf(dest.user_path, sizeof(dest.user_path), "%s", devices[index].user_path);
                dest.pose.pose = devices[index].pose;
                dest.pose.timestamp = static_cast<uint64_t>(predicted_time);
                dest.pose.flags.store(devices[index].is_active, std::memory_order_release);
                dest.is_active = devices[index].is_active;
            }

            if (driver_callbacks_.submit_frame_pixels) {
                for (uint32_t eye_index = 0; eye_index < 2; ++eye_index) {
                    auto& texture = frame.textures[eye_index];
                    if (texture.ready.load(std::memory_order_acquire) == 1) {
                        texture.ready.store(0, std::memory_order_release);
                        driver_callbacks_.submit_frame_pixels(eye_index, texture.width.load(std::memory_order_relaxed),
                                                              texture.height.load(std::memory_order_relaxed),
                                                              texture.format.load(std::memory_order_relaxed),
                                                              texture.pixel_data,
                                                              texture.data_size.load(std::memory_order_relaxed));
                    }
                }
            }
        }

        std::this_thread::sleep_until(next_frame);
    }
}

void IpcServer::InitializeSharedData() {
    std::memset(shared_data_, 0, sizeof(SharedData));
    shared_data_->protocol_version.store(PROTOCOL_VERSION, std::memory_order_release);
    shared_data_->backend_ready.store(1, std::memory_order_release);
    shared_data_->frontend_connected.store(0, std::memory_order_release);
}

void IpcServer::HandleConnect(const MessageHeader& request) { SendEmpty(control_channel_, request); }

void IpcServer::HandleMetadataRequest(const MessageHeader& request, MessageType type) {
    switch (type) {
        case MessageType::GET_DEVICE_INFO: {
            OxDeviceInfo info{};
            driver_callbacks_.get_device_info(&info);
            SendPayload(control_channel_, request, info);
            break;
        }
        case MessageType::GET_DISPLAY_PROPERTIES: {
            OxDisplayProperties props{};
            driver_callbacks_.get_display_properties(&props);
            SendPayload(control_channel_, request, props);
            break;
        }
        case MessageType::GET_TRACKING_CAPABILITIES: {
            OxTrackingCapabilities caps{};
            driver_callbacks_.get_tracking_capabilities(&caps);
            SendPayload(control_channel_, request, caps);
            break;
        }
        case MessageType::GET_INTERACTION_PROFILES: {
            InteractionProfilesResponse response{};
            if (driver_callbacks_.get_interaction_profiles) {
                const char* profiles[MAX_INTERACTION_PROFILES] = {};
                response.profile_count =
                    std::min<uint32_t>(driver_callbacks_.get_interaction_profiles(profiles, MAX_INTERACTION_PROFILES),
                                       MAX_INTERACTION_PROFILES);
                for (uint32_t index = 0; index < response.profile_count; ++index) {
                    snprintf(response.profiles[index], sizeof(response.profiles[index]), "%s", profiles[index]);
                }
            } else {
                response.profile_count = 1;
                snprintf(response.profiles[0], sizeof(response.profiles[0]), "%s",
                         "/interaction_profiles/khr/simple_controller");
            }
            SendPayload(control_channel_, request, response);
            break;
        }
        default:
            SendEmpty(control_channel_, request);
            break;
    }
}

void IpcServer::HandleInputRequest(const MessageHeader& request, const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(InputStateRequest)) {
        SendEmpty(control_channel_, request);
        return;
    }

    const auto* input = reinterpret_cast<const InputStateRequest*>(payload.data());
    switch (request.type) {
        case MessageType::GET_INPUT_STATE_BOOLEAN: {
            InputStateBooleanResponse response{};
            if (driver_callbacks_.get_input_state_boolean) {
                response.is_available = driver_callbacks_.get_input_state_boolean(
                                            input->predicted_time, input->user_path, input->component_path,
                                            &response.value) == OX_COMPONENT_AVAILABLE;
            }
            SendPayload(control_channel_, request, response);
            break;
        }
        case MessageType::GET_INPUT_STATE_FLOAT: {
            InputStateFloatResponse response{};
            if (driver_callbacks_.get_input_state_float) {
                response.is_available = driver_callbacks_.get_input_state_float(
                                            input->predicted_time, input->user_path, input->component_path,
                                            &response.value) == OX_COMPONENT_AVAILABLE;
            }
            SendPayload(control_channel_, request, response);
            break;
        }
        case MessageType::GET_INPUT_STATE_VECTOR2F: {
            InputStateVector2fResponse response{};
            if (driver_callbacks_.get_input_state_vector2f) {
                response.is_available = driver_callbacks_.get_input_state_vector2f(
                                            input->predicted_time, input->user_path, input->component_path,
                                            &response.value) == OX_COMPONENT_AVAILABLE;
            }
            SendPayload(control_channel_, request, response);
            break;
        }
        default:
            SendEmpty(control_channel_, request);
            break;
    }
}

void IpcServer::HandleSessionState(const MessageHeader& request, const std::vector<uint8_t>& payload) {
    if (payload.size() >= sizeof(SessionStateNotification) && driver_callbacks_.on_session_state_changed) {
        const auto* notification = reinterpret_cast<const SessionStateNotification*>(payload.data());
        driver_callbacks_.on_session_state_changed(notification->state);
    }
    SendEmpty(control_channel_, request);
}

}  // namespace ipc
}  // namespace ox