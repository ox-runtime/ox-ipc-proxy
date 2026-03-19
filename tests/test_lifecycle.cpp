#include "common.hpp"

TEST_F(IpcTest, Connect_Succeeds) {
    Start();
    EXPECT_TRUE(IsFrontendConnected());
}

TEST(IpcLifecycle, Connect_WhenNoServerRunning_Fails) {
    OxDriverCallbacks frontend_callbacks{};
    ASSERT_EQ(ox_driver_register(&frontend_callbacks), 1);
    ASSERT_NE(frontend_callbacks.initialize, nullptr);
    EXPECT_EQ(frontend_callbacks.initialize(), 0);
    if (frontend_callbacks.shutdown) {
        frontend_callbacks.shutdown();
    }
}

TEST_F(IpcTest, Disconnect_ThenReconnect_Succeeds) {
    Start();

    ASSERT_NE(driver().shutdown, nullptr);
    driver().shutdown();
    EXPECT_FALSE(IsFrontendConnected());
    ASSERT_NE(driver().initialize, nullptr);
    EXPECT_EQ(driver().initialize(), 1);
    EXPECT_TRUE(IsFrontendConnected());
}
