#include <gtest/gtest.h>
#include <thread>

#include "common/network/address.h"
#include "quic/quicx/ip_rate_limiter.h"

using namespace quicx::quic;
using namespace quicx::common;

class IPRateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create limiter with small cache and low threshold for testing
        // max_cache_size=100, rate_threshold=5, window_seconds=60
        limiter_ = std::make_unique<IPRateLimiter>(100, 5, 60);
        
        // Setup test addresses
        addr1_.SetIp("192.168.1.1");
        addr1_.SetPort(12345);
        
        addr2_.SetIp("192.168.1.2");
        addr2_.SetPort(12345);
    }

    std::unique_ptr<IPRateLimiter> limiter_;
    Address addr1_;
    Address addr2_;
};

TEST_F(IPRateLimiterTest, InitialState) {
    EXPECT_EQ(0, limiter_->GetCacheSize());
    EXPECT_FALSE(limiter_->IsSuspicious(addr1_));
}

TEST_F(IPRateLimiterTest, RecordConnection) {
    limiter_->RecordConnection(addr1_);
    
    EXPECT_EQ(1, limiter_->GetCacheSize());
    EXPECT_EQ(1, limiter_->GetConnectionCount(addr1_.GetIp()));
}

TEST_F(IPRateLimiterTest, MultipleConnectionsSameIP) {
    for (int i = 0; i < 3; ++i) {
        limiter_->RecordConnection(addr1_);
    }
    
    EXPECT_EQ(1, limiter_->GetCacheSize());  // Still 1 IP
    EXPECT_EQ(3, limiter_->GetConnectionCount(addr1_.GetIp()));
}

TEST_F(IPRateLimiterTest, MultipleIPs) {
    limiter_->RecordConnection(addr1_);
    limiter_->RecordConnection(addr2_);
    
    EXPECT_EQ(2, limiter_->GetCacheSize());
    EXPECT_EQ(1, limiter_->GetConnectionCount(addr1_.GetIp()));
    EXPECT_EQ(1, limiter_->GetConnectionCount(addr2_.GetIp()));
}

TEST_F(IPRateLimiterTest, SuspiciousIPDetection) {
    // Below threshold (5)
    for (int i = 0; i < 4; ++i) {
        limiter_->RecordConnection(addr1_);
    }
    EXPECT_FALSE(limiter_->IsSuspicious(addr1_));
    
    // Reach threshold
    limiter_->RecordConnection(addr1_);
    EXPECT_TRUE(limiter_->IsSuspicious(addr1_));
    
    // Above threshold
    limiter_->RecordConnection(addr1_);
    EXPECT_TRUE(limiter_->IsSuspicious(addr1_));
}

TEST_F(IPRateLimiterTest, NonSuspiciousIPUnaffected) {
    // Make addr1 suspicious
    for (int i = 0; i < 10; ++i) {
        limiter_->RecordConnection(addr1_);
    }
    
    // addr2 should not be suspicious
    limiter_->RecordConnection(addr2_);
    EXPECT_FALSE(limiter_->IsSuspicious(addr2_));
    EXPECT_TRUE(limiter_->IsSuspicious(addr1_));
}

TEST_F(IPRateLimiterTest, UnknownIPNotSuspicious) {
    Address unknown_addr;
    unknown_addr.SetIp("10.0.0.1");
    unknown_addr.SetPort(8888);
    
    EXPECT_FALSE(limiter_->IsSuspicious(unknown_addr));
    EXPECT_EQ(0, limiter_->GetConnectionCount(unknown_addr.GetIp()));
}

TEST_F(IPRateLimiterTest, StringIPInterface) {
    limiter_->RecordConnection("192.168.1.100");
    limiter_->RecordConnection("192.168.1.100");
    
    EXPECT_EQ(2, limiter_->GetConnectionCount("192.168.1.100"));
    EXPECT_FALSE(limiter_->IsSuspicious("192.168.1.100"));
}

TEST_F(IPRateLimiterTest, Clear) {
    limiter_->RecordConnection(addr1_);
    limiter_->RecordConnection(addr2_);
    
    EXPECT_EQ(2, limiter_->GetCacheSize());
    
    limiter_->Clear();
    
    EXPECT_EQ(0, limiter_->GetCacheSize());
    EXPECT_EQ(0, limiter_->GetConnectionCount(addr1_.GetIp()));
}

TEST_F(IPRateLimiterTest, LRUEviction) {
    // Create limiter with small cache
    auto small_limiter = std::make_unique<IPRateLimiter>(3, 5, 60);
    
    // Add 3 IPs
    small_limiter->RecordConnection("192.168.1.1");
    small_limiter->RecordConnection("192.168.1.2");
    small_limiter->RecordConnection("192.168.1.3");
    
    EXPECT_EQ(3, small_limiter->GetCacheSize());
    
    // Add 4th IP, should evict the least recently used (192.168.1.1)
    small_limiter->RecordConnection("192.168.1.4");
    
    EXPECT_EQ(3, small_limiter->GetCacheSize());
    EXPECT_EQ(0, small_limiter->GetConnectionCount("192.168.1.1"));  // Evicted
    EXPECT_EQ(1, small_limiter->GetConnectionCount("192.168.1.4"));  // New entry
}

TEST_F(IPRateLimiterTest, LRUTouchOnAccess) {
    // Create limiter with small cache
    auto small_limiter = std::make_unique<IPRateLimiter>(3, 5, 60);
    
    // Add 3 IPs
    small_limiter->RecordConnection("192.168.1.1");
    small_limiter->RecordConnection("192.168.1.2");
    small_limiter->RecordConnection("192.168.1.3");
    
    // Touch the oldest entry (192.168.1.1), making it most recent
    small_limiter->RecordConnection("192.168.1.1");
    
    // Add new IP, should evict 192.168.1.2 (now the oldest)
    small_limiter->RecordConnection("192.168.1.4");
    
    EXPECT_EQ(3, small_limiter->GetCacheSize());
    EXPECT_EQ(2, small_limiter->GetConnectionCount("192.168.1.1"));  // Still here
    EXPECT_EQ(0, small_limiter->GetConnectionCount("192.168.1.2"));  // Evicted
}

TEST_F(IPRateLimiterTest, UpdateConfig) {
    // Initially not suspicious
    for (int i = 0; i < 4; ++i) {
        limiter_->RecordConnection(addr1_);
    }
    EXPECT_FALSE(limiter_->IsSuspicious(addr1_));
    
    // Lower threshold to 3
    limiter_->UpdateConfig(3, 60);
    
    // Now should be suspicious (count=4 > threshold=3)
    EXPECT_TRUE(limiter_->IsSuspicious(addr1_));
}

TEST_F(IPRateLimiterTest, ConcurrentAccess) {
    const int kThreads = 4;
    const int kConnectionsPerThread = 100;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([this, i, kConnectionsPerThread]() {
            Address addr;
            addr.SetIp("192.168.1." + std::to_string(i + 1));
            addr.SetPort(12345);
            
            for (int j = 0; j < kConnectionsPerThread; ++j) {
                limiter_->RecordConnection(addr);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Each IP should have correct count
    EXPECT_EQ(kThreads, limiter_->GetCacheSize());
}

// Note: Window expiration test would require time manipulation
// which is complex without a mock clock. In production, consider
// using a testable clock interface for more thorough testing.
