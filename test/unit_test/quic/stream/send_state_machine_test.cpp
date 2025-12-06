#include <gtest/gtest.h>
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/state_machine_send.h"

namespace quicx {
namespace quic {
namespace {

TEST(send_state_machine_utest, normal_state_change) {
    StreamStateMachineSend state(nullptr);
    EXPECT_EQ(state.GetStatus(), StreamState::kReady);

    EXPECT_FALSE(state.OnFrame(FrameType::kStopSending));
    EXPECT_FALSE(state.OnFrame(FrameType::kPadding));

    EXPECT_TRUE(state.OnFrame(FrameType::kStream));
    EXPECT_EQ(state.GetStatus(), StreamState::kSend);

    EXPECT_TRUE(state.OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag));
    EXPECT_EQ(state.GetStatus(), StreamState::kDataSent);

    EXPECT_TRUE(state.AllAckDone());
}

TEST(send_state_machine_utest, wrong_state_change) {
    StreamStateMachineSend state(nullptr);
    EXPECT_EQ(state.GetStatus(), StreamState::kReady);

    EXPECT_FALSE(state.AllAckDone());

    EXPECT_TRUE(state.OnFrame(FrameType::kStream));
    EXPECT_EQ(state.GetStatus(), StreamState::kSend);

    EXPECT_TRUE(state.OnFrame(FrameType::kResetStream));
    EXPECT_EQ(state.GetStatus(), StreamState::kResetSent);

    EXPECT_FALSE(state.OnFrame(FrameType::kStream));
    EXPECT_TRUE(state.AllAckDone());
}

}
}
}