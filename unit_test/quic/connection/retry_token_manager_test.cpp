#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "common/network/address.h"
#include "quic/connection/connection_id.h"
#include "quic/connection/retry_token_manager.h"

using namespace quicx::quic;
using namespace quicx::common;

class RetryTokenManagerTest: public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<RetryTokenManager>();

        // Setup test address
        addr_.SetIp("192.168.1.100");
        addr_.SetPort(12345);

        // Setup test connection ID
        uint8_t cid_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        dcid_ = ConnectionID(cid_data, sizeof(cid_data));
    }

    std::unique_ptr<RetryTokenManager> manager_;
    Address addr_;
    ConnectionID dcid_;
};

TEST_F(RetryTokenManagerTest, GenerateToken) {
    std::string token = manager_->GenerateToken(addr_, dcid_);

    // Token should be timestamp (8 bytes) + HMAC (32 bytes) = 40 bytes
    EXPECT_EQ(40, token.size());
}

TEST_F(RetryTokenManagerTest, ValidateValidToken) {
    // Generate token
    std::string token = manager_->GenerateToken(addr_, dcid_);

    // Validate immediately (should succeed)
    EXPECT_TRUE(manager_->ValidateToken(token, addr_, dcid_));
}

TEST_F(RetryTokenManagerTest, ValidateExpiredToken) {
    // Generate token
    std::string token = manager_->GenerateToken(addr_, dcid_);

    // Validate with max_age = 0 (should fail)
    EXPECT_FALSE(manager_->ValidateToken(token, addr_, dcid_, 0));
}

TEST_F(RetryTokenManagerTest, ValidateWrongAddress) {
    // Generate token for addr1
    std::string token = manager_->GenerateToken(addr_, dcid_);

    // Try to validate with different address
    Address wrong_addr;
    wrong_addr.SetIp("192.168.1.101");
    wrong_addr.SetPort(12345);

    EXPECT_FALSE(manager_->ValidateToken(token, wrong_addr, dcid_));
}

TEST_F(RetryTokenManagerTest, ValidateWrongConnectionId) {
    // Generate token for dcid1
    std::string token = manager_->GenerateToken(addr_, dcid_);

    // Try to validate with different connection ID
    uint8_t wrong_cid_data[] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    ConnectionID wrong_dcid(wrong_cid_data, sizeof(wrong_cid_data));

    EXPECT_FALSE(manager_->ValidateToken(token, addr_, wrong_dcid));
}

TEST_F(RetryTokenManagerTest, ValidateInvalidTokenSize) {
    std::string invalid_token = "too_short";

    EXPECT_FALSE(manager_->ValidateToken(invalid_token, addr_, dcid_));
}

TEST_F(RetryTokenManagerTest, ValidateCorruptedToken) {
    // Generate valid token
    std::string token = manager_->GenerateToken(addr_, dcid_);

    // Corrupt the HMAC part
    token[20] ^= 0xFF;

    EXPECT_FALSE(manager_->ValidateToken(token, addr_, dcid_));
}

TEST_F(RetryTokenManagerTest, SecretRotation) {
    // Generate token with current secret
    std::string token1 = manager_->GenerateToken(addr_, dcid_);

    // Validate (should succeed)
    EXPECT_TRUE(manager_->ValidateToken(token1, addr_, dcid_));

    // Rotate secret
    manager_->RotateSecret();

    // Old token should still be valid (previous secret kept)
    EXPECT_TRUE(manager_->ValidateToken(token1, addr_, dcid_));

    // Generate new token with new secret
    std::string token2 = manager_->GenerateToken(addr_, dcid_);

    // New token should be valid
    EXPECT_TRUE(manager_->ValidateToken(token2, addr_, dcid_));

    // Rotate again
    manager_->RotateSecret();

    // token1 should now be invalid (too old)
    EXPECT_FALSE(manager_->ValidateToken(token1, addr_, dcid_));

    // token2 should still be valid (previous secret)
    EXPECT_TRUE(manager_->ValidateToken(token2, addr_, dcid_));
}

TEST_F(RetryTokenManagerTest, MultipleTokens) {
    // Generate multiple tokens
    std::string token1 = manager_->GenerateToken(addr_, dcid_);

    Address addr2;
    addr2.SetIp("10.0.0.1");
    addr2.SetPort(54321);
    std::string token2 = manager_->GenerateToken(addr2, dcid_);

    // Each token should validate with its own address
    EXPECT_TRUE(manager_->ValidateToken(token1, addr_, dcid_));
    EXPECT_TRUE(manager_->ValidateToken(token2, addr2, dcid_));

    // But not with the other address
    EXPECT_FALSE(manager_->ValidateToken(token1, addr2, dcid_));
    EXPECT_FALSE(manager_->ValidateToken(token2, addr_, dcid_));
}

TEST_F(RetryTokenManagerTest, TokenUniqueness) {
    // Generate two tokens for the same address/dcid
    std::string token1 = manager_->GenerateToken(addr_, dcid_);

    // Wait a bit to ensure different timestamp
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::string token2 = manager_->GenerateToken(addr_, dcid_);

    // Tokens should be different (different timestamps)
    EXPECT_NE(token1, token2);

    // But both should be valid
    EXPECT_TRUE(manager_->ValidateToken(token1, addr_, dcid_));
    EXPECT_TRUE(manager_->ValidateToken(token2, addr_, dcid_));
}

TEST_F(RetryTokenManagerTest, ConcurrentAccess) {
    const int num_threads = 10;
    const int tokens_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &success_count, i]() {
            Address local_addr;
            local_addr.SetIp("192.168.1." + std::to_string(i));
            local_addr.SetPort(10000 + i);

            for (int j = 0; j < tokens_per_thread; ++j) {
                std::string token = manager_->GenerateToken(local_addr, dcid_);
                if (manager_->ValidateToken(token, local_addr, dcid_)) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All tokens should have been validated successfully
    EXPECT_EQ(num_threads * tokens_per_thread, success_count.load());
}
