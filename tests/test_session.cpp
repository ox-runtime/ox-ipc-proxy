#include "common.hpp"

TEST_F(IpcTest, SessionState_DeliveredToDriver) {
    Start();

    driver().on_session_state_changed(OX_SESSION_STATE_FOCUSED);
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(mock.last_session_state, OX_SESSION_STATE_FOCUSED);
}

TEST_F(IpcTest, SessionState_AllTransitions_DoNotCrash) {
    Start();

    for (int state = OX_SESSION_STATE_IDLE; state <= OX_SESSION_STATE_EXITING; ++state) {
        EXPECT_NO_FATAL_FAILURE(driver().on_session_state_changed(static_cast<OxSessionState>(state)));
    }
}
