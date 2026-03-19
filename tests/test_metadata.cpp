#include "common.hpp"

TEST_F(IpcTest, DeviceInfo_ReturnsExactValues) {
    mock.device_name = "Oculus Quest 3";
    mock.manufacturer = "Meta";
    mock.serial = "OQ3-9876";

    Start();

    OxDeviceInfo info{};
    driver().get_device_info(&info);
    EXPECT_STREQ(info.name, "Oculus Quest 3");
    EXPECT_STREQ(info.manufacturer, "Meta");
    EXPECT_STREQ(info.serial, "OQ3-9876");
}

TEST_F(IpcTest, DisplayProperties_ReturnsExactValues) {
    mock.display_width = 2448;
    mock.display_height = 2448;
    mock.refresh_rate = 120.0f;

    Start();

    OxDisplayProperties props{};
    driver().get_display_properties(&props);
    EXPECT_EQ(props.display_width, 2448u);
    EXPECT_EQ(props.display_height, 2448u);
    EXPECT_FLOAT_EQ(props.refresh_rate, 120.0f);
}

TEST_F(IpcTest, TrackingCapabilities_NoPositionTracking) {
    mock.has_position = 0;
    mock.has_orientation = 1;

    Start();

    OxTrackingCapabilities caps{};
    driver().get_tracking_capabilities(&caps);
    EXPECT_EQ(caps.has_position_tracking, 0u);
    EXPECT_EQ(caps.has_orientation_tracking, 1u);
}

TEST_F(IpcTest, InteractionProfiles_ReturnsAllProfiles) {
    mock.profiles = {
        "/interaction_profiles/khr/simple_controller",
        "/interaction_profiles/valve/index_controller",
    };

    Start();

    const char* profiles[8]{};
    const uint32_t count = driver().get_interaction_profiles(profiles, 8);
    ASSERT_EQ(count, 2u);
    EXPECT_STREQ(profiles[0], "/interaction_profiles/khr/simple_controller");
    EXPECT_STREQ(profiles[1], "/interaction_profiles/valve/index_controller");
}

TEST_F(IpcTest, InteractionProfiles_TruncatedWhenBufferSmall) {
    mock.profiles = {"a", "b", "c"};

    Start();

    const char* profiles[2]{};
    const uint32_t total = driver().get_interaction_profiles(profiles, 2);
    EXPECT_EQ(total, 3u);
}
