#include <gtest/gtest.h>
#include <thread>

#include "quic/quicx/connection_rate_monitor.h"

using namespace quicx::quic;

class ConnectionRateMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create monitor without event loop (manual rate calculation for testing)
        monitor_ = std::make_unique<ConnectionRateMonitor>(nullptr);
    }

    std::unique_ptr<ConnectionRateMonitor> monitor_;
};

TEST_F(ConnectionRateMonitorTest, InitialState) {
    // Initial state should be zero
    EXPECT_EQ(0, monitor_->GetConnectionRate());
    EXPECT_EQ(0, monitor_->GetCurrentCount());
    EXPECT_FALSE(monitor_->IsHighRate(100));
}

TEST_F(ConnectionRateMonitorTest, RecordConnections) {
    // Record some connections
    monitor_->RecordNewConnection();
    monitor_->RecordNewConnection();
    monitor_->RecordNewConnection();
    
    // Current count should reflect recorded connections
    EXPECT_EQ(3, monitor_->GetCurrentCount());
    
    // Rate is still 0 until CalculateRate is called
    EXPECT_EQ(0, monitor_->GetConnectionRate());
}

TEST_F(ConnectionRateMonitorTest, CalculateRate) {
    // Record connections
    monitor_->RecordNewConnection();
    monitor_->RecordNewConnection();
    monitor_->RecordNewConnection();
    monitor_->RecordNewConnection();
    monitor_->RecordNewConnection();
    
    // Manually trigger rate calculation
    monitor_->CalculateRate();
    
    // Rate should now be 5
    EXPECT_EQ(5, monitor_->GetConnectionRate());
    
    // Current count should be reset to 0
    EXPECT_EQ(0, monitor_->GetCurrentCount());
}

TEST_F(ConnectionRateMonitorTest, IsHighRateWithLastRate) {
    // Record connections and calculate rate
    for (int i = 0; i < 150; ++i) {
        monitor_->RecordNewConnection();
    }
    monitor_->CalculateRate();
    
    // Should be high rate for threshold 100
    EXPECT_TRUE(monitor_->IsHighRate(100));
    
    // Should not be high rate for threshold 200
    EXPECT_FALSE(monitor_->IsHighRate(200));
}

TEST_F(ConnectionRateMonitorTest, IsHighRateWithCurrentCount) {
    // Record connections but don't calculate rate yet
    for (int i = 0; i < 150; ++i) {
        monitor_->RecordNewConnection();
    }
    
    // Even without CalculateRate, IsHighRate should detect high current count
    EXPECT_TRUE(monitor_->IsHighRate(100));
}

TEST_F(ConnectionRateMonitorTest, RateResetAfterCalculation) {
    // Record connections
    for (int i = 0; i < 100; ++i) {
        monitor_->RecordNewConnection();
    }
    monitor_->CalculateRate();
    EXPECT_EQ(100, monitor_->GetConnectionRate());
    
    // Record fewer connections
    for (int i = 0; i < 20; ++i) {
        monitor_->RecordNewConnection();
    }
    monitor_->CalculateRate();
    
    // Rate should reflect new window
    EXPECT_EQ(20, monitor_->GetConnectionRate());
}

TEST_F(ConnectionRateMonitorTest, ConcurrentAccess) {
    const int kThreads = 4;
    const int kConnectionsPerThread = 1000;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([this]() {
            for (int j = 0; j < kConnectionsPerThread; ++j) {
                monitor_->RecordNewConnection();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // All connections should be recorded
    EXPECT_EQ(kThreads * kConnectionsPerThread, monitor_->GetCurrentCount());
}

TEST_F(ConnectionRateMonitorTest, ZeroThreshold) {
    // Any count should exceed zero threshold
    monitor_->RecordNewConnection();
    EXPECT_TRUE(monitor_->IsHighRate(0));
}
