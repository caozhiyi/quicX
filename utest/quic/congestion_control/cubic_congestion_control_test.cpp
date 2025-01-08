#include <gtest/gtest.h>
#include "quic/congestion_control/cubic_congestion_control.h"

namespace quicx {
namespace quic {
namespace {

class CubicCongestionControlTest : public testing::Test {
protected:
    void SetUp() override {
        cubic_ = std::make_unique<CubicCongestionControl>();
    }

    std::unique_ptr<CubicCongestionControl> cubic_;
};

TEST_F(CubicCongestionControlTest, InitialState) {
    EXPECT_EQ(cubic_->GetCongestionWindow(), cubic_->GetInitialWindow());
    EXPECT_EQ(cubic_->GetBytesInFlight(), 0);
    EXPECT_TRUE(cubic_->CanSend(0));
}

TEST_F(CubicCongestionControlTest, OnPacketSent) {
    size_t bytes_sent = 1000;
    cubic_->OnPacketSent(bytes_sent, 0);
    EXPECT_EQ(cubic_->GetBytesInFlight(), bytes_sent);
}

TEST_F(CubicCongestionControlTest, OnPacketAcked) {
    size_t bytes_sent = 1000;
    cubic_->OnPacketSent(bytes_sent, 0);
    cubic_->OnPacketAcked(bytes_sent, 0);
    EXPECT_EQ(cubic_->GetBytesInFlight(), 0);
    EXPECT_GT(cubic_->GetCongestionWindow(), cubic_->GetInitialWindow());
}

TEST_F(CubicCongestionControlTest, OnPacketLost) {
    size_t bytes_sent = 1000;
    cubic_->OnPacketSent(bytes_sent, 0);
    cubic_->OnPacketLost(bytes_sent, 0);
    EXPECT_EQ(cubic_->GetBytesInFlight(), 0);
    EXPECT_EQ(cubic_->GetCongestionWindow(), cubic_->GetInitialWindow());
}

TEST_F(CubicCongestionControlTest, CongestionAvoidance) {
    size_t initial_cwnd = cubic_->GetCongestionWindow();
    size_t bytes_sent = initial_cwnd / 2;
    cubic_->OnPacketSent(bytes_sent, 0);
    cubic_->OnPacketAcked(bytes_sent, 0);
    EXPECT_GT(cubic_->GetCongestionWindow(), initial_cwnd);
}

TEST_F(CubicCongestionControlTest, SlowStart) {
    size_t initial_cwnd = cubic_->GetCongestionWindow();
    size_t bytes_sent = initial_cwnd;
    cubic_->OnPacketSent(bytes_sent, 0);
    cubic_->OnPacketAcked(bytes_sent, 0);
    EXPECT_GT(cubic_->GetCongestionWindow(), initial_cwnd);
}

TEST_F(CubicCongestionControlTest, Reset) {
    cubic_->OnPacketSent(1000, 0);
    cubic_->Reset();
    EXPECT_EQ(cubic_->GetCongestionWindow(), cubic_->GetInitialWindow());
    EXPECT_EQ(cubic_->GetBytesInFlight(), 0);
}

}  // namespace
}  // namespace quic
}  // namespace quicx 