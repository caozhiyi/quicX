#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "http3/stream/type.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/stream/unidentified_stream.h"
#include "unit_test/http3/stream/mock_quic_stream.h"
#include "http3/stream/qpack_encoder_sender_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockSenderConnection {
public:
    MockSenderConnection(std::shared_ptr<quic::IQuicSendStream> stream) 
        : error_code_(0) {
        sender_stream_ = std::make_shared<QpackEncoderSenderStream>(stream,
            std::bind(&MockSenderConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockSenderConnection() {}

    bool SendSetCapacity(uint64_t capacity) {
        return sender_stream_->SendSetCapacity(capacity);
    }

    bool SendInsertWithNameRef(bool is_static, uint64_t name_index, const std::string& value) {
        return sender_stream_->SendInsertWithNameRef(is_static, name_index, value);
    }

    bool SendInsertWithoutNameRef(const std::string& name, const std::string& value) {
        return sender_stream_->SendInsertWithoutNameRef(name, value);
    }

    bool SendDuplicate(uint64_t index) {
        return sender_stream_->SendDuplicate(index);
    }

    bool SendInstructions(const std::vector<uint8_t>& blob) {
        return sender_stream_->SendInstructions(blob);
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    uint32_t GetErrorCode() { return error_code_; }

private:
    uint32_t error_code_;
    std::shared_ptr<QpackEncoderSenderStream> sender_stream_;
};

class MockReceiverConnection {
public:
    MockReceiverConnection(std::shared_ptr<quic::IQuicRecvStream> stream) 
        : error_code_(0), notify_count_(0) {
        
        blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
        
        // Start with UnidentifiedStream to read stream type
        unidentified_stream_ = std::make_shared<UnidentifiedStream>(stream,
            std::bind(&MockReceiverConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockReceiverConnection::OnStreamTypeIdentified, this, 
                     std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }
    ~MockReceiverConnection() {}

    void OnStreamTypeIdentified(uint64_t stream_type, 
                                std::shared_ptr<quic::IQuicRecvStream> stream,
                                std::shared_ptr<common::IBufferRead> remaining_data) {
        // Verify it's a QPACK encoder stream
        EXPECT_EQ(stream_type, static_cast<uint64_t>(StreamType::kQpackEncoder));
        
        // Create the actual QPACK encoder receiver stream
        receiver_stream_ = std::make_shared<QpackEncoderReceiverStream>(stream,
            blocked_registry_,
            std::bind(&MockReceiverConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
        
        // Feed remaining data to the encoder stream
        if (remaining_data && remaining_data->GetDataLength() > 0) {
            receiver_stream_->OnData(remaining_data, 0);
        }
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }
    
    void NotifyCallback() {
        notify_count_++;
    }

    uint32_t GetErrorCode() { return error_code_; }
    uint32_t GetNotifyCount() { return notify_count_; }
    std::shared_ptr<QpackBlockedRegistry> GetBlockedRegistry() { return blocked_registry_; }

private:
    uint32_t error_code_;
    uint32_t notify_count_;
    
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::shared_ptr<UnidentifiedStream> unidentified_stream_;
    std::shared_ptr<QpackEncoderReceiverStream> receiver_stream_;
};

// Test sending and receiving Set Dynamic Table Capacity instruction
TEST(QpackEncoderStreamTest, SetCapacityTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send Set Capacity instruction
    uint64_t capacity = 4096;
    EXPECT_TRUE(sender->SendSetCapacity(capacity));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test sending multiple different capacities
TEST(QpackEncoderStreamTest, SetCapacityMultipleTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send multiple capacity updates
    EXPECT_TRUE(sender->SendSetCapacity(0));        // Minimum
    EXPECT_TRUE(sender->SendSetCapacity(4096));     // Typical
    EXPECT_TRUE(sender->SendSetCapacity(16384));    // Larger
    EXPECT_TRUE(sender->SendSetCapacity(1048576));  // 1MB - very large
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Insert With Name Reference (static table)
TEST(QpackEncoderStreamTest, InsertWithNameRefStaticTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Insert entry referencing static table
    // For example, name_index=0 (:authority) with custom value
    bool is_static = true;
    uint64_t name_index = 0;
    std::string value = "example.com";
    
    EXPECT_TRUE(sender->SendInsertWithNameRef(is_static, name_index, value));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Insert With Name Reference (dynamic table)
TEST(QpackEncoderStreamTest, InsertWithNameRefDynamicTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Insert entry referencing dynamic table
    bool is_static = false;
    uint64_t name_index = 5;
    std::string value = "custom-value";
    
    EXPECT_TRUE(sender->SendInsertWithNameRef(is_static, name_index, value));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Insert With Name Reference with various values
TEST(QpackEncoderStreamTest, InsertWithNameRefVariousValuesTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Test with empty value
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 0, ""));
    
    // Test with short value
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 1, "x"));
    
    // Test with typical value
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 2, "application/json"));
    
    // Test with long value
    std::string long_value(256, 'a');
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 3, long_value));
    
    // Test with UTF-8 characters
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 4, "测试-テスト-🎉"));
    
    // Test with large index
    EXPECT_TRUE(sender->SendInsertWithNameRef(false, 100, "value"));
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Insert Without Name Reference
TEST(QpackEncoderStreamTest, InsertWithoutNameRefTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Insert entry with both name and value
    std::string name = "x-custom-header";
    std::string value = "custom-value";
    
    EXPECT_TRUE(sender->SendInsertWithoutNameRef(name, value));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Insert Without Name Reference with various combinations
TEST(QpackEncoderStreamTest, InsertWithoutNameRefVariousTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Test with both empty
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("", ""));
    
    // Test with empty value
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("x-header", ""));
    
    // Test with empty name (edge case)
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("", "value"));
    
    // Test with typical headers
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("content-type", "text/html"));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("cache-control", "no-cache, no-store, must-revalidate"));
    
    // Test with long name and value
    std::string long_name(128, 'n');
    std::string long_value(256, 'v');
    EXPECT_TRUE(sender->SendInsertWithoutNameRef(long_name, long_value));
    
    // Test with special characters
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("x-special-chars", "!@#$%^&*()_+-=[]{}|;:',.<>?/~`"));
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Duplicate instruction
TEST(QpackEncoderStreamTest, DuplicateTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send Duplicate instruction
    uint64_t index = 3;
    EXPECT_TRUE(sender->SendDuplicate(index));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test Duplicate with various indices
TEST(QpackEncoderStreamTest, DuplicateVariousIndicesTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Test with index 0
    EXPECT_TRUE(sender->SendDuplicate(0));
    
    // Test with small indices
    EXPECT_TRUE(sender->SendDuplicate(1));
    EXPECT_TRUE(sender->SendDuplicate(5));
    EXPECT_TRUE(sender->SendDuplicate(10));
    
    // Test with larger indices
    EXPECT_TRUE(sender->SendDuplicate(100));
    EXPECT_TRUE(sender->SendDuplicate(1000));
    
    // Test with very large index (varint encoding)
    EXPECT_TRUE(sender->SendDuplicate(65535));
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test sending raw instructions blob
TEST(QpackEncoderStreamTest, SendInstructionsBlobTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send raw instruction bytes (example: Set Capacity)
    std::vector<uint8_t> instructions = {0x3f, 0x11};  // Set capacity to some value
    EXPECT_TRUE(sender->SendInstructions(instructions));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    
    // Test with empty blob
    std::vector<uint8_t> empty_blob;
    EXPECT_TRUE(sender->SendInstructions(empty_blob));
    
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test multiple instructions in sequence
TEST(QpackEncoderStreamTest, MultipleInstructionsSequenceTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send a sequence of various instructions
    EXPECT_TRUE(sender->SendSetCapacity(8192));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("header-1", "value-1"));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("header-2", "value-2"));
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 0, "example.com"));
    EXPECT_TRUE(sender->SendDuplicate(0));
    EXPECT_TRUE(sender->SendInsertWithNameRef(false, 1, "new-value"));
    EXPECT_TRUE(sender->SendSetCapacity(16384));  // Update capacity
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test interleaved operations
TEST(QpackEncoderStreamTest, InterleavedOperationsTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Simulate realistic usage pattern
    // 1. Set initial capacity
    EXPECT_TRUE(sender->SendSetCapacity(4096));
    
    // 2. Insert some common headers
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("content-type", "application/json"));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("content-length", "1234"));
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 0, "api.example.com"));  // :authority
    
    // 3. Duplicate a frequently used header
    EXPECT_TRUE(sender->SendDuplicate(0));
    
    // 4. Add more headers
    EXPECT_TRUE(sender->SendInsertWithNameRef(false, 0, "application/xml"));  // Reuse dynamic table name
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("x-request-id", "abc-123-def"));
    
    // 5. Increase capacity
    EXPECT_TRUE(sender->SendSetCapacity(8192));
    
    // 6. Add more entries
    for (int i = 0; i < 10; i++) {
        std::string name = "x-header-" + std::to_string(i);
        std::string value = "value-" + std::to_string(i);
        EXPECT_TRUE(sender->SendInsertWithoutNameRef(name, value));
    }
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test edge cases with boundary values
TEST(QpackEncoderStreamTest, BoundaryValuesTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Test with zero capacity
    EXPECT_TRUE(sender->SendSetCapacity(0));
    
    // Test with maximum reasonable capacity
    EXPECT_TRUE(sender->SendSetCapacity(0xFFFFFFFF));
    
    // Test with index 0
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 0, "value"));
    EXPECT_TRUE(sender->SendDuplicate(0));
    
    // Test with single character strings
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("a", "b"));
    
    // Test with long strings (within buffer limits)
    // SendInsertWithoutNameRef uses a 1024 byte buffer, so name + value + overhead must fit
    std::string very_long_name(256, 'n');  // 256 bytes for name
    std::string very_long_value(512, 'v'); // 512 bytes for value (fits in 1024 byte buffer)
    EXPECT_TRUE(sender->SendInsertWithoutNameRef(very_long_name, very_long_value));
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test that encoder instructions trigger NotifyAll on blocked registry
TEST(QpackEncoderStreamTest, BlockedRegistryNotificationTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Add some blocked entries to the receiver's registry
    uint64_t key1 = (1ULL << 32) | 1;
    uint64_t key2 = (2ULL << 32) | 2;
    
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(key1, 
        std::bind(&MockReceiverConnection::NotifyCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(key2, 
        std::bind(&MockReceiverConnection::NotifyCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 2);
    
    // Send encoder instructions - should trigger NotifyAll
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("new-header", "new-value"));
    
    // Check that blocked entries were notified and cleared
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetNotifyCount(), 2);
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test rapid succession of instructions
TEST(QpackEncoderStreamTest, RapidInstructionsTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send many instructions rapidly
    for (int i = 0; i < 100; i++) {
        if (i % 5 == 0) {
            EXPECT_TRUE(sender->SendSetCapacity(4096 + i * 100));
        } else if (i % 5 == 1) {
            EXPECT_TRUE(sender->SendInsertWithoutNameRef("name-" + std::to_string(i), "value-" + std::to_string(i)));
        } else if (i % 5 == 2) {
            EXPECT_TRUE(sender->SendInsertWithNameRef(true, i % 10, "value-" + std::to_string(i)));
        } else if (i % 5 == 3) {
            EXPECT_TRUE(sender->SendInsertWithNameRef(false, i % 20, "value-" + std::to_string(i)));
        } else {
            EXPECT_TRUE(sender->SendDuplicate(i % 30));
        }
    }
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test mixed instruction types in batches
TEST(QpackEncoderStreamTest, BatchedMixedInstructionsTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Batch 1: Initial setup
    EXPECT_TRUE(sender->SendSetCapacity(4096));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("accept", "*/*"));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("accept-encoding", "gzip, deflate, br"));
    
    // Batch 2: Add more using references
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 1, "www.example.com"));  // :authority
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 2, "/api/v1/users"));    // :path
    EXPECT_TRUE(sender->SendInsertWithNameRef(true, 3, "POST"));              // :method
    
    // Batch 3: Duplicate frequently used headers
    EXPECT_TRUE(sender->SendDuplicate(0));
    EXPECT_TRUE(sender->SendDuplicate(1));
    
    // Batch 4: Add custom headers
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("x-api-key", "secret-key-123"));
    EXPECT_TRUE(sender->SendInsertWithoutNameRef("x-correlation-id", "uuid-1234-5678"));
    
    // Batch 5: Increase capacity for more entries
    EXPECT_TRUE(sender->SendSetCapacity(8192));
    
    // Batch 6: Add more entries referencing dynamic table
    EXPECT_TRUE(sender->SendInsertWithNameRef(false, 0, "text/html"));  // Reuse "accept" name
    EXPECT_TRUE(sender->SendInsertWithNameRef(false, 1, "identity"));   // Reuse "accept-encoding" name
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

}  // namespace
}  // namespace http3
}  // namespace quicx

