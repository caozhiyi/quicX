#include <gtest/gtest.h>
#include "quic/congestion_control/reno_congestion_control.h"

namespace quicx {
namespace quic {
namespace {

class RenoCongestionControlTest : public testing::Test {
protected:
    void SetUp() override {
        reno_ = std::make_unique<RenoCongestionControl>();
    }

    std::unique_ptr<RenoCongestionControl> reno_;
};

TEST_F(RenoCongestionControlTest, InitialState) {
    EXPECT_EQ(reno_->GetCongestionWindow(), reno_->GetInitialWindow());
    EXPECT_EQ(reno_->GetBytesInFlight(), 0);
    //EXPECT_TRUE(reno_->CanSend(0));
}

TEST_F(RenoCongestionControlTest, OnPacketSent) {
    size_t bytes_sent = 1000;
    reno_->OnPacketSent(bytes_sent, 0);
    EXPECT_EQ(reno_->GetBytesInFlight(), bytes_sent);
}

TEST_F(RenoCongestionControlTest, OnPacketAcked) {
    size_t bytes_sent = 1000;
    reno_->OnPacketSent(bytes_sent, 0);
    reno_->OnPacketAcked(bytes_sent, 0);
    EXPECT_EQ(reno_->GetBytesInFlight(), 0);
    EXPECT_GT(reno_->GetCongestionWindow(), reno_->GetInitialWindow());
}

TEST_F(RenoCongestionControlTest, OnPacketLost) {
    size_t bytes_sent = 1000;
    reno_->OnPacketSent(bytes_sent, 0);
    reno_->OnPacketLost(bytes_sent, 0);
    EXPECT_EQ(reno_->GetBytesInFlight(), 0);
    EXPECT_LT(reno_->GetCongestionWindow(), reno_->GetInitialWindow());
}

TEST_F(RenoCongestionControlTest, CongestionAvoidance) {
    size_t initial_cwnd = reno_->GetCongestionWindow();
    size_t bytes_sent = initial_cwnd / 2;
    reno_->OnPacketSent(bytes_sent, 0);
    reno_->OnPacketAcked(bytes_sent, 0);
    EXPECT_GT(reno_->GetCongestionWindow(), initial_cwnd);
}

TEST_F(RenoCongestionControlTest, SlowStart) {
    size_t initial_cwnd = reno_->GetCongestionWindow();
    size_t bytes_sent = initial_cwnd;
    reno_->OnPacketSent(bytes_sent, 0);
    reno_->OnPacketAcked(bytes_sent, 0);
    EXPECT_GT(reno_->GetCongestionWindow(), initial_cwnd);
}

TEST_F(RenoCongestionControlTest, Reset) {
    reno_->OnPacketSent(1000, 0);
    reno_->Reset();
    EXPECT_EQ(reno_->GetCongestionWindow(), reno_->GetInitialWindow());
    EXPECT_EQ(reno_->GetBytesInFlight(), 0);
}

}  // namespace
}  // namespace quic
}  // namespace quicx 