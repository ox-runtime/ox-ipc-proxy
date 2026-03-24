#include "common.hpp"

TEST_F(IpcTest, ViewPose_LeftEye_ReturnsDriverValues) {
    mock.view_pose[0] = {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 2.0f, -3.0f}};

    Start();
    WaitForFrame();

    XrView view{XR_TYPE_VIEW};
    driver().update_view(0, 0, &view);
    EXPECT_NEAR(view.pose.position.x, 1.0f, 1e-5f);
    EXPECT_NEAR(view.pose.position.y, 2.0f, 1e-5f);
    EXPECT_NEAR(view.pose.position.z, -3.0f, 1e-5f);
}

TEST_F(IpcTest, ViewPose_RightEye_ReturnsDriverValues) {
    mock.view_pose[1] = {{0.0f, 0.0f, 0.0f, 1.0f}, {-0.032f, 0.0f, 0.0f}};

    Start();
    WaitForFrame();

    XrView view{XR_TYPE_VIEW};
    driver().update_view(0, 1, &view);
    EXPECT_NEAR(view.pose.position.x, -0.032f, 1e-4f);
}

TEST_F(IpcTest, ViewPose_InvalidEyeIndex_DoesNotCrash) {
    Start();

    XrView view{XR_TYPE_VIEW};
    EXPECT_NO_FATAL_FAILURE(driver().update_view(0, 99, &view));
}

TEST_F(IpcTest, UpdateDevices_ReturnsAllDevices) {
    OxDeviceState head{};
    std::snprintf(head.user_path, sizeof(head.user_path), "%s", "/user/head");
    head.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.7f, 0.0f}};
    head.is_active = 1;
    mock.devices.push_back(head);

    Start();
    WaitForFrame();

    OxDeviceState states[OX_MAX_DEVICES]{};
    uint32_t count = 0;
    driver().update_devices(0, states, &count);
    ASSERT_EQ(count, 1u);
    EXPECT_STREQ(states[0].user_path, "/user/head");
    EXPECT_EQ(states[0].is_active, 1u);
    EXPECT_NEAR(states[0].pose.position.y, 1.7f, 1e-5f);
}

TEST_F(IpcTest, UpdateDevices_NoDevices_ReturnsZeroCount) {
    Start();
    WaitForFrame();

    uint32_t count = 0;
    driver().update_devices(0, nullptr, &count);
    EXPECT_EQ(count, 0u);
}

TEST_F(IpcTest, SubmitFramePixels_ValidEye_DoesNotCrash) {
    Start();

    const uint32_t width = 4;
    const uint32_t height = 4;
    std::vector<uint8_t> pixels(width * height * 4, 200u);
    EXPECT_NO_FATAL_FAILURE(
        driver().submit_frame_pixels(0, 0, width, height, 0x8058, pixels.data(), static_cast<uint32_t>(pixels.size())));
    EXPECT_TRUE(IsFrontendConnected());
}

TEST_F(IpcTest, SubmitFramePixels_OversizedData_DoesNotDisconnect) {
    Start();

    std::vector<uint8_t> pixels(ox::ipc::MAX_TEXTURE_SIZE + 1, 0u);
    EXPECT_NO_FATAL_FAILURE(
        driver().submit_frame_pixels(0, 0, 1, 1, 0, pixels.data(), static_cast<uint32_t>(pixels.size())));
    EXPECT_TRUE(IsFrontendConnected());
}
