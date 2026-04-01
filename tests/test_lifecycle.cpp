#include "../src/shared_memory.h"
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

TEST(IpcLifecycle, BackendInitialize_ReplacesStaleSharedMemory) {
    MockState mock;
    OxDriverCallbacks callbacks = mock.MakeCallbacks();

    ox::protocol::SharedMemory stale_shared_memory;
    ASSERT_TRUE(stale_shared_memory.Create(ox::ipc::SHARED_MEMORY_NAME, sizeof(ox::ipc::SharedData), true));
    stale_shared_memory.Close();

    ox_ipc_backend_set_driver(&callbacks);
    EXPECT_EQ(ox_ipc_backend_initialize(), 1);
    ox_ipc_backend_shutdown();
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
