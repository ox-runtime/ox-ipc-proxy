#include "../src/shared_memory.h"
#include "common.hpp"

TEST_F(IpcTest, Connect_Succeeds) {
    Start();
    EXPECT_TRUE(IsClientConnected());
}

TEST(IpcLifecycle, Connect_WhenNoServerRunning_Fails) {
    OxDriver client_driver{};
    ASSERT_EQ(ox_driver_register(&client_driver), 1);
    ASSERT_NE(client_driver.initialize, nullptr);
    EXPECT_EQ(client_driver.initialize(), 0);
    if (client_driver.shutdown) {
        client_driver.shutdown();
    }
}

TEST(IpcLifecycle, ServerInitialize_ReplacesStaleSharedMemory) {
    MockState mock;
    OxDriver driver = mock.MakeDriver();

    ox::protocol::SharedMemory stale_shared_memory;
    ASSERT_TRUE(stale_shared_memory.Create(ox::ipc::SHARED_MEMORY_NAME, sizeof(ox::ipc::SharedData), true));
    stale_shared_memory.Close();

    ox_ipc_server_set_driver(&driver);
    EXPECT_EQ(ox_ipc_server_initialize(), 1);
    ox_ipc_server_shutdown();
}

TEST_F(IpcTest, Disconnect_ThenReconnect_Succeeds) {
    Start();

    ASSERT_NE(driver().shutdown, nullptr);
    driver().shutdown();
    EXPECT_FALSE(IsClientConnected());
    ASSERT_NE(driver().initialize, nullptr);
    EXPECT_EQ(driver().initialize(), 1);
    EXPECT_TRUE(IsClientConnected());
}
