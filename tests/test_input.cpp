#include "common.hpp"

TEST_F(IpcTest, Input_Boolean_AvailableAndCorrectValue) {
    mock.SetBool("/user/hand/left", "/input/trigger/click", true, 1u);

    Start();
    WaitForFrame();

    XrBool32 value = XR_FALSE;
    EXPECT_EQ(driver().get_input_state_boolean(0, "/user/hand/left", "/input/trigger/click", &value), XR_SUCCESS);
    EXPECT_EQ(value, XR_TRUE);
}

TEST_F(IpcTest, Input_Boolean_UnavailableComponent) {
    mock.SetBool("/user/hand/left", "/input/trigger/click", false, 0u);

    Start();
    WaitForFrame();

    XrBool32 value = XR_FALSE;
    EXPECT_EQ(driver().get_input_state_boolean(0, "/user/hand/left", "/input/trigger/click", &value),
              XR_ERROR_PATH_UNSUPPORTED);
}

TEST_F(IpcTest, Input_Boolean_UnknownPath_ReturnsUnavailable) {
    Start();
    WaitForFrame();

    XrBool32 value = XR_FALSE;
    EXPECT_EQ(driver().get_input_state_boolean(0, "/user/hand/left", "/input/x/click", &value),
              XR_ERROR_PATH_UNSUPPORTED);
}

TEST_F(IpcTest, Input_Float_CorrectValue) {
    mock.SetFloat("/user/hand/right", "/input/trigger/value", true, 0.75f);

    Start();
    WaitForFrame();

    float value = 0.0f;
    EXPECT_EQ(driver().get_input_state_float(0, "/user/hand/right", "/input/trigger/value", &value), XR_SUCCESS);
    EXPECT_NEAR(value, 0.75f, 1e-5f);
}

TEST_F(IpcTest, Input_Float_ZeroValue) {
    mock.SetFloat("/user/hand/left", "/input/squeeze/value", true, 0.0f);

    Start();
    WaitForFrame();

    float value = -1.0f;
    driver().get_input_state_float(0, "/user/hand/left", "/input/squeeze/value", &value);
    EXPECT_NEAR(value, 0.0f, 1e-5f);
}

TEST_F(IpcTest, Input_Vector2f_CorrectValue) {
    mock.SetVec2f("/user/hand/left", "/input/thumbstick", true, {0.5f, -0.3f});

    Start();
    WaitForFrame();

    XrVector2f value{};
    EXPECT_EQ(driver().get_input_state_vector2f(0, "/user/hand/left", "/input/thumbstick", &value), XR_SUCCESS);
    EXPECT_NEAR(value.x, 0.5f, 1e-5f);
    EXPECT_NEAR(value.y, -0.3f, 1e-5f);
}

TEST_F(IpcTest, Input_SamePathRegisteredTwice_NoDuplicateSlot) {
    mock.SetFloat("/user/hand/left", "/input/trigger/value", true, 0.5f);

    Start();
    WaitForFrame();

    float first = 0.0f;
    float second = 0.0f;
    driver().get_input_state_float(0, "/user/hand/left", "/input/trigger/value", &first);
    driver().get_input_state_float(0, "/user/hand/left", "/input/trigger/value", &second);
    EXPECT_NEAR(first, second, 1e-5f);
}
