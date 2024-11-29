#include <gtest/gtest.h>
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/state_machine_recv.h"

namespace quicx {
namespace quic {
namespace {

TEST(recv_state_machine_utest, normal_state_change) {
    StreamStateMachineRecv state(nullptr);
    EXPECT_EQ(state.GetStatus(), SS_RECV);

    EXPECT_FALSE(state.OnFrame(FT_STOP_SENDING));
    EXPECT_FALSE(state.OnFrame(FT_PADDING));

    EXPECT_TRUE(state.OnFrame(FT_STREAM));
    EXPECT_EQ(state.GetStatus(), SS_RECV);

    EXPECT_TRUE(state.OnFrame(FT_STREAM | SFF_FIN));
    EXPECT_EQ(state.GetStatus(), SS_SIZE_KNOWN);

    EXPECT_TRUE(state.RecvAllData());

    EXPECT_EQ(state.GetStatus(), SS_DATA_RECVD);

    EXPECT_TRUE(state.AppReadAllData());

    EXPECT_EQ(state.GetStatus(), SS_DATA_READ);
}

TEST(recv_state_machine_utest, wrong_state_change) {
    StreamStateMachineRecv state(nullptr);
    EXPECT_EQ(state.GetStatus(), SS_RECV);

    EXPECT_FALSE(state.RecvAllData());

    EXPECT_TRUE(state.OnFrame(FT_STREAM));
    EXPECT_EQ(state.GetStatus(), SS_RECV);

    EXPECT_TRUE(state.OnFrame(FT_RESET_STREAM));
    EXPECT_EQ(state.GetStatus(), SS_RESET_RECVD);

    EXPECT_TRUE(state.OnFrame(FT_STREAM));
    EXPECT_TRUE(state.RecvAllData());

    EXPECT_TRUE(state.AppReadAllData());

    EXPECT_EQ(state.GetStatus(), SS_RESET_READ);
}

}
}
}