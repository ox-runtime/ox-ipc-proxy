#pragma once

#include <gtest/gtest.h>
#include <openxr/openxr.h>
#include <ox_driver.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "../src/messages.h"

extern "C" {
void ox_ipc_backend_set_driver(const OxDriverCallbacks* callbacks);
int ox_ipc_backend_initialize();
void ox_ipc_backend_shutdown();
int ox_driver_register(OxDriverCallbacks* callbacks);
}

using namespace std::chrono_literals;

struct MockState {
    std::string device_name = "Test HMD";
    std::string manufacturer = "Test";
    std::string serial = "TEST-001";
    uint32_t vendor_id = 0;
    uint32_t product_id = 0;

    uint32_t display_width = 1920;
    uint32_t display_height = 1080;
    float refresh_rate = 90.0f;
    XrFovf fov = {-0.8f, 0.8f, 0.8f, -0.8f};

    uint32_t has_position = 1;
    uint32_t has_orientation = 1;

    XrPosef view_pose[2] = {};
    std::vector<OxDeviceState> devices;
    std::vector<std::string> profiles = {"/interaction_profiles/khr/simple_controller"};

    struct InputEntry {
        ox::ipc::InputSlotType type;
        bool available = true;
        union {
            uint32_t b;
            float f;
            XrVector2f v;
        } value{};
    };

    std::map<std::string, InputEntry> inputs;
    XrSessionState last_session_state = XR_SESSION_STATE_UNKNOWN;

    void SetBool(const char* user, const char* comp, bool available, uint32_t value) {
        InputEntry entry{};
        entry.type = ox::ipc::InputSlotType::BOOLEAN;
        entry.available = available;
        entry.value.b = value;
        inputs[std::string(user) + "|" + comp] = entry;
    }

    void SetFloat(const char* user, const char* comp, bool available, float value) {
        InputEntry entry{};
        entry.type = ox::ipc::InputSlotType::FLOAT;
        entry.available = available;
        entry.value.f = value;
        inputs[std::string(user) + "|" + comp] = entry;
    }

    void SetVec2f(const char* user, const char* comp, bool available, XrVector2f value) {
        InputEntry entry{};
        entry.type = ox::ipc::InputSlotType::VECTOR2F;
        entry.available = available;
        entry.value.v = value;
        inputs[std::string(user) + "|" + comp] = entry;
    }

    OxDriverCallbacks MakeCallbacks();
};

inline MockState* g_mock = nullptr;

inline OxDriverCallbacks MockState::MakeCallbacks() {
    g_mock = this;

    OxDriverCallbacks callbacks{};
    callbacks.initialize = []() -> int { return 1; };
    callbacks.shutdown = []() {};
    callbacks.is_device_connected = []() -> int { return 1; };

    callbacks.get_device_info = [](OxDeviceInfo* info) {
        std::snprintf(info->name, sizeof(info->name), "%s", g_mock->device_name.c_str());
        std::snprintf(info->manufacturer, sizeof(info->manufacturer), "%s", g_mock->manufacturer.c_str());
        std::snprintf(info->serial, sizeof(info->serial), "%s", g_mock->serial.c_str());
        info->vendor_id = g_mock->vendor_id;
        info->product_id = g_mock->product_id;
    };

    callbacks.get_display_properties = [](OxDisplayProperties* props) {
        props->display_width = g_mock->display_width;
        props->display_height = g_mock->display_height;
        props->recommended_width = g_mock->display_width;
        props->recommended_height = g_mock->display_height;
        props->refresh_rate = g_mock->refresh_rate;
        props->fov = g_mock->fov;
    };

    callbacks.get_tracking_capabilities = [](OxTrackingCapabilities* caps) {
        caps->has_position_tracking = g_mock->has_position;
        caps->has_orientation_tracking = g_mock->has_orientation;
    };

    callbacks.update_view_pose = [](XrTime, uint32_t eye_index, XrPosef* out_pose) {
        if (eye_index < 2 && out_pose) {
            *out_pose = g_mock->view_pose[eye_index];
        }
    };

    callbacks.update_devices = [](XrTime, OxDeviceState* out_states, uint32_t* out_count) {
        const uint32_t count =
            static_cast<uint32_t>(std::min(g_mock->devices.size(), static_cast<size_t>(OX_MAX_DEVICES)));
        *out_count = count;
        for (uint32_t index = 0; index < count; ++index) {
            out_states[index] = g_mock->devices[index];
        }
    };

    callbacks.get_interaction_profiles = [](const char** profiles, uint32_t max_profiles) -> uint32_t {
        const uint32_t count =
            static_cast<uint32_t>(std::min(g_mock->profiles.size(), static_cast<size_t>(max_profiles)));
        for (uint32_t index = 0; index < count; ++index) {
            profiles[index] = g_mock->profiles[index].c_str();
        }
        return static_cast<uint32_t>(g_mock->profiles.size());
    };

    callbacks.on_session_state_changed = [](XrSessionState state) { g_mock->last_session_state = state; };

    callbacks.get_input_state_boolean = [](XrTime, const char* user_path, const char* component_path,
                                           uint32_t* out_value) -> OxComponentResult {
        const auto found = g_mock->inputs.find(std::string(user_path) + "|" + component_path);
        if (found == g_mock->inputs.end() || !found->second.available) {
            return OX_COMPONENT_UNAVAILABLE;
        }
        if (out_value) {
            *out_value = found->second.value.b;
        }
        return OX_COMPONENT_AVAILABLE;
    };

    callbacks.get_input_state_float = [](XrTime, const char* user_path, const char* component_path,
                                         float* out_value) -> OxComponentResult {
        const auto found = g_mock->inputs.find(std::string(user_path) + "|" + component_path);
        if (found == g_mock->inputs.end() || !found->second.available) {
            return OX_COMPONENT_UNAVAILABLE;
        }
        if (out_value) {
            *out_value = found->second.value.f;
        }
        return OX_COMPONENT_AVAILABLE;
    };

    callbacks.get_input_state_vector2f = [](XrTime, const char* user_path, const char* component_path,
                                            XrVector2f* out_value) -> OxComponentResult {
        const auto found = g_mock->inputs.find(std::string(user_path) + "|" + component_path);
        if (found == g_mock->inputs.end() || !found->second.available) {
            return OX_COMPONENT_UNAVAILABLE;
        }
        if (out_value) {
            *out_value = found->second.value.v;
        }
        return OX_COMPONENT_AVAILABLE;
    };

    return callbacks;
}

class IpcTest : public ::testing::Test {
   protected:
    MockState mock;
    OxDriverCallbacks frontend_callbacks_{};

    void Start() {
        OxDriverCallbacks callbacks = mock.MakeCallbacks();
        ox_ipc_backend_set_driver(&callbacks);
        ASSERT_EQ(ox_ipc_backend_initialize(), 1);
        ASSERT_EQ(ox_driver_register(&frontend_callbacks_), 1);
        ASSERT_NE(frontend_callbacks_.initialize, nullptr);
        ASSERT_EQ(frontend_callbacks_.initialize(), 1);
    }

    static void WaitForFrame() { std::this_thread::sleep_for(30ms); }

    void TearDown() override {
        if (frontend_callbacks_.shutdown) {
            frontend_callbacks_.shutdown();
        }
        frontend_callbacks_ = {};
        ox_ipc_backend_shutdown();
        g_mock = nullptr;
    }

    OxDriverCallbacks& driver() { return frontend_callbacks_; }

    bool IsFrontendConnected() const {
        return frontend_callbacks_.is_device_connected && frontend_callbacks_.is_device_connected() != 0;
    }
};
