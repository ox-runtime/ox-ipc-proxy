#include "common.hpp"

TEST_F(IpcTest, SystemProperties_ReturnsExactValues) {
    mock.device_name = "Oculus Quest 3";
    mock.vendor_id = 0xFB;
    mock.display_width = 2448;
    mock.display_height = 2448;
    mock.has_position = 1;
    mock.has_orientation = 1;

    Start();

    XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
    driver().get_system_properties(&props);
    EXPECT_STREQ(props.systemName, "Oculus Quest 3");
    EXPECT_EQ(props.vendorId, 0xFBu);
    EXPECT_EQ(props.graphicsProperties.maxSwapchainImageWidth, 2448u);
    EXPECT_EQ(props.graphicsProperties.maxSwapchainImageHeight, 2448u);
    EXPECT_EQ(props.trackingProperties.positionTracking, XR_TRUE);
    EXPECT_EQ(props.trackingProperties.orientationTracking, XR_TRUE);
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
