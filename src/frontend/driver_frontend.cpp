#include <ox_driver.h>

#include "ipc_client.h"

namespace {

ox::ipc::IpcClient& Client() { return ox::ipc::IpcClient::Instance(); }

int FrontendInitialize() { return Client().Connect() ? 1 : 0; }

void FrontendShutdown() { Client().Disconnect(); }

int FrontendIsDeviceConnected() { return Client().IsConnected() ? 1 : 0; }

void FrontendGetDeviceInfo(OxDeviceInfo* info) { Client().GetDeviceInfo(info); }

void FrontendGetDisplayProperties(OxDisplayProperties* props) { Client().GetDisplayProperties(props); }

void FrontendGetTrackingCapabilities(OxTrackingCapabilities* caps) { Client().GetTrackingCapabilities(caps); }

void FrontendUpdateViewPose(int64_t predicted_time, uint32_t eye_index, OxPose* out_pose) {
    Client().UpdateViewPose(predicted_time, eye_index, out_pose);
}

void FrontendUpdateDevices(int64_t predicted_time, OxDeviceState* out_states, uint32_t* out_count) {
    Client().UpdateDevices(predicted_time, out_states, out_count);
}

OxComponentResult FrontendGetInputStateBoolean(int64_t predicted_time, const char* user_path,
                                               const char* component_path, uint32_t* out_value) {
    return Client().GetInputStateBoolean(predicted_time, user_path, component_path, out_value);
}

OxComponentResult FrontendGetInputStateFloat(int64_t predicted_time, const char* user_path, const char* component_path,
                                             float* out_value) {
    return Client().GetInputStateFloat(predicted_time, user_path, component_path, out_value);
}

OxComponentResult FrontendGetInputStateVector2f(int64_t predicted_time, const char* user_path,
                                                const char* component_path, OxVector2f* out_value) {
    return Client().GetInputStateVector2f(predicted_time, user_path, component_path, out_value);
}

uint32_t FrontendGetInteractionProfiles(const char** profiles, uint32_t max_profiles) {
    return Client().GetInteractionProfiles(profiles, max_profiles);
}

void FrontendNotifySessionState(OxSessionState state) { Client().NotifySessionState(state); }

void FrontendSubmitFramePixels(uint32_t eye_index, uint32_t width, uint32_t height, uint32_t format,
                               const void* pixel_data, uint32_t data_size) {
    Client().SubmitFramePixels(eye_index, width, height, format, pixel_data, data_size);
}

}  // namespace

extern "C" OX_DRIVER_EXPORT int ox_driver_register(OxDriverCallbacks* callbacks) {
    if (!callbacks) {
        return 0;
    }

    callbacks->initialize = &FrontendInitialize;
    callbacks->shutdown = &FrontendShutdown;
    callbacks->is_device_connected = &FrontendIsDeviceConnected;
    callbacks->get_device_info = &FrontendGetDeviceInfo;
    callbacks->get_display_properties = &FrontendGetDisplayProperties;
    callbacks->get_tracking_capabilities = &FrontendGetTrackingCapabilities;
    callbacks->update_view_pose = &FrontendUpdateViewPose;
    callbacks->update_devices = &FrontendUpdateDevices;
    callbacks->get_input_state_boolean = &FrontendGetInputStateBoolean;
    callbacks->get_input_state_float = &FrontendGetInputStateFloat;
    callbacks->get_input_state_vector2f = &FrontendGetInputStateVector2f;
    callbacks->get_interaction_profiles = &FrontendGetInteractionProfiles;
    callbacks->on_session_state_changed = &FrontendNotifySessionState;
    callbacks->submit_frame_pixels = &FrontendSubmitFramePixels;
    return 1;
}