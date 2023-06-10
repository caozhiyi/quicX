#include <gtest/gtest.h>
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/send_state_machine.h"

namespace quicx {
namespace {

TEST(send_state_machine_utest, normal_state_change) {
    SendStreamStateMachine state;
    EXPECT_EQ(state.GetStatus(), SS_READY);

    EXPECT_FALSE(state.OnFrame(FT_STOP_SENDING));
    EXPECT_FALSE(state.OnFrame(FT_PADDING));

    EXPECT_TRUE(state.OnFrame(FT_STREAM));
    EXPECT_EQ(state.GetStatus(), SS_SEND);

    EXPECT_TRUE(state.OnFrame(FT_STREAM | SFF_FIN));
    EXPECT_EQ(state.GetStatus(), SS_DATA_SENT);

    EXPECT_TRUE(state.AllAckDone());
}

TEST(send_state_machine_utest, wrong_state_change) {
    SendStreamStateMachine state;
    EXPECT_EQ(state.GetStatus(), SS_READY);

    EXPECT_FALSE(state.AllAckDone());

    EXPECT_TRUE(state.OnFrame(FT_STREAM));
    EXPECT_EQ(state.GetStatus(), SS_SEND);

    EXPECT_TRUE(state.OnFrame(FT_RESET_STREAM));
    EXPECT_EQ(state.GetStatus(), SS_RESET_SENT);

    EXPECT_FALSE(state.OnFrame(FT_STREAM));
    EXPECT_TRUE(state.AllAckDone());
}

}
}