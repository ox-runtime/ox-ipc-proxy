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
void ox_ipc_server_set_driver(const OxDriver* driver);
int ox_ipc_server_initialize();
void ox_ipc_server_shutdown();
int ox_driver_register(OxDriver* driver);
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

    OxDriver MakeDriver();
};

inline MockState* g_mock = nullptr;

inline OxDriver MockState::MakeDriver() {
    g_mock = this;

    OxDriver driver{};
    driver.initialize = []() -> int { return 1; };
    driver.shutdown = []() {};
    driver.is_device_connected = []() -> int { return 1; };

    driver.get_system_properties = [](XrSystemProperties* props) {
        if (!props) return;
        void* next = props->next;
        props->vendorId = g_mock->vendor_id;
        std::snprintf(props->systemName, XR_MAX_SYSTEM_NAME_SIZE, "%s", g_mock->device_name.c_str());
        props->graphicsProperties.maxSwapchainImageWidth = g_mock->display_width;
        props->graphicsProperties.maxSwapchainImageHeight = g_mock->display_height;
        props->graphicsProperties.maxLayerCount = 16;
        props->trackingProperties.orientationTracking = g_mock->has_orientation ? XR_TRUE : XR_FALSE;
        props->trackingProperties.positionTracking = g_mock->has_position ? XR_TRUE : XR_FALSE;
        props->next = next;
    };

    driver.update_view = [](XrTime, uint32_t eye_index, XrView* out_view) {
        if (eye_index < 2 && out_view) {
            out_view->pose = g_mock->view_pose[eye_index];
            out_view->fov = {-0.785398f, 0.785398f, 0.785398f, -0.785398f};
        }
    };

    driver.update_devices = [](XrTime, OxDeviceState* out_states, uint32_t* out_count) {
        const uint32_t count =
            static_cast<uint32_t>(std::min(g_mock->devices.size(), static_cast<size_t>(OX_MAX_DEVICES)));
        *out_count = count;
        for (uint32_t index = 0; index < count; ++index) {
            out_states[index] = g_mock->devices[index];
        }
    };

    driver.get_interaction_profiles = [](const char** profiles, uint32_t max_profiles) -> uint32_t {
        const uint32_t count =
            static_cast<uint32_t>(std::min(g_mock->profiles.size(), static_cast<size_t>(max_profiles)));
        for (uint32_t index = 0; index < count; ++index) {
            profiles[index] = g_mock->profiles[index].c_str();
        }
        return static_cast<uint32_t>(g_mock->profiles.size());
    };

    driver.on_session_state_changed = [](XrSessionState state) { g_mock->last_session_state = state; };

    driver.get_input_state_bool = [](XrTime, const char* user_path, const char* component_path,
                                     XrBool32* out_value) -> XrResult {
        const auto found = g_mock->inputs.find(std::string(user_path) + "|" + component_path);
        if (found == g_mock->inputs.end() || !found->second.available) {
            return XR_ERROR_PATH_UNSUPPORTED;
        }
        if (out_value) {
            *out_value = found->second.value.b ? XR_TRUE : XR_FALSE;
        }
        return XR_SUCCESS;
    };

    driver.get_input_state_float = [](XrTime, const char* user_path, const char* component_path,
                                      float* out_value) -> XrResult {
        const auto found = g_mock->inputs.find(std::string(user_path) + "|" + component_path);
        if (found == g_mock->inputs.end() || !found->second.available) {
            return XR_ERROR_PATH_UNSUPPORTED;
        }
        if (out_value) {
            *out_value = found->second.value.f;
        }
        return XR_SUCCESS;
    };

    driver.get_input_state_vector2f = [](XrTime, const char* user_path, const char* component_path,
                                         XrVector2f* out_value) -> XrResult {
        const auto found = g_mock->inputs.find(std::string(user_path) + "|" + component_path);
        if (found == g_mock->inputs.end() || !found->second.available) {
            return XR_ERROR_PATH_UNSUPPORTED;
        }
        if (out_value) {
            *out_value = found->second.value.v;
        }
        return XR_SUCCESS;
    };

    return driver;
}

class IpcTest : public ::testing::Test {
   protected:
    MockState mock;
    OxDriver client_driver_{};

    void Start() {
        OxDriver driver = mock.MakeDriver();
        ox_ipc_server_set_driver(&driver);
        ASSERT_EQ(ox_ipc_server_initialize(), 1);
        ASSERT_EQ(ox_driver_register(&client_driver_), 1);
        ASSERT_NE(client_driver_.initialize, nullptr);
        ASSERT_EQ(client_driver_.initialize(), 1);
    }

    static void WaitForFrame() { std::this_thread::sleep_for(100ms); }

    void TearDown() override {
        if (client_driver_.shutdown) {
            client_driver_.shutdown();
        }
        client_driver_ = {};
        ox_ipc_server_shutdown();
        g_mock = nullptr;
    }

    OxDriver& driver() { return client_driver_; }

    bool IsClientConnected() const {
        return client_driver_.is_device_connected && client_driver_.is_device_connected() != 0;
    }
};
