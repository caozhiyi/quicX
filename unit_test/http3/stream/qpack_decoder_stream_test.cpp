#include <gtest/gtest.h>
#include <memory>

#include "http3/stream/type.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/stream/unidentified_stream.h"
#include "unit_test/http3/stream/mock_quic_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockSenderConnection {
public:
    MockSenderConnection(std::shared_ptr<quic::IQuicSendStream> stream) 
        : error_code_(0) {
        sender_stream_ = std::make_shared<QpackDecoderSenderStream>(stream,
            std::bind(&MockSenderConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockSenderConnection() {}

    bool SendSectionAck(uint64_t header_block_id) {
        return sender_stream_->SendSectionAck(header_block_id);
    }

    bool SendStreamCancel(uint64_t header_block_id) {
        return sender_stream_->SendStreamCancel(header_block_id);
    }

    bool SendInsertCountIncrement(uint64_t delta) {
        return sender_stream_->SendInsertCountIncrement(delta);
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }

    uint32_t GetErrorCode() { return error_code_; }

private:
    uint32_t error_code_;
    std::shared_ptr<QpackDecoderSenderStream> sender_stream_;
};

class MockReceiverConnection {
public:
    MockReceiverConnection(std::shared_ptr<quic::IQuicRecvStream> stream) 
        : error_code_(0), retry_count_(0) {
        
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
        // Verify it's a QPACK decoder stream
        EXPECT_EQ(stream_type, static_cast<uint64_t>(StreamType::kQpackDecoder));
        
        // Create the actual QPACK decoder receiver stream
        receiver_stream_ = std::make_shared<QpackDecoderReceiverStream>(stream,
            blocked_registry_,
            std::bind(&MockReceiverConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
        
        // Feed remaining data to the decoder stream
        if (remaining_data && remaining_data->GetDataLength() > 0) {
            receiver_stream_->OnData(remaining_data, 0);
        }
    }

    void ErrorHandle(uint64_t stream_id, uint32_t error_code) {
        error_code_ = error_code;
    }
    
    void RetryCallback() {
        retry_count_++;
    }

    uint32_t GetErrorCode() { return error_code_; }
    uint32_t GetRetryCount() { return retry_count_; }
    std::shared_ptr<QpackBlockedRegistry> GetBlockedRegistry() { return blocked_registry_; }

private:
    uint32_t error_code_;
    uint32_t retry_count_;
    
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::shared_ptr<UnidentifiedStream> unidentified_stream_;
    std::shared_ptr<QpackDecoderReceiverStream> receiver_stream_;
};

// Test sending and receiving Section Acknowledgement frame
TEST(QpackDecoderStreamTest, SectionAckTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send Section Ack with header_block_id (stream_id=5, section_number=10)
    uint64_t stream_id = 5;
    uint64_t section_number = 10;
    uint64_t header_block_id = (stream_id << 32) | section_number;
    
    // Add to blocked registry first with a retry callback
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    
    EXPECT_TRUE(sender->SendSectionAck(header_block_id));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    
    // After receiving Section Ack, it should be unblocked (retry callback called and removed)
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 1);  // Retry callback was called
}

// Test sending and receiving Stream Cancellation frame
TEST(QpackDecoderStreamTest, StreamCancelTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Send Stream Cancellation with header_block_id (stream_id=7, section_number=20)
    uint64_t stream_id = 7;
    uint64_t section_number = 20;
    uint64_t header_block_id = (stream_id << 32) | section_number;
    
    // Add to blocked registry first (callback won't be called for Remove)
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    
    EXPECT_TRUE(sender->SendStreamCancel(header_block_id));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    
    // After receiving Stream Cancellation, it should be removed (without calling callback)
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 0);  // Retry callback NOT called for cancellation
}

// Test sending and receiving Insert Count Increment frame
TEST(QpackDecoderStreamTest, InsertCountIncrementTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Add some blocked entries
    uint64_t header_block_id_1 = (1ULL << 32) | 1;
    uint64_t header_block_id_2 = (2ULL << 32) | 2;
    
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_1, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_2, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 2);
    
    // Send Insert Count Increment - should trigger NotifyAll which retries and clears all
    uint64_t delta = 5;
    EXPECT_TRUE(sender->SendInsertCountIncrement(delta));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    
    // After Insert Count Increment, NotifyAll should be called
    // which triggers all retry callbacks and clears the registry
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 2);  // Both callbacks called
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test sending multiple frames in sequence
TEST(QpackDecoderStreamTest, MultipleFramesTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Prepare blocked registry with multiple entries
    uint64_t header_block_id_1 = (1ULL << 32) | 1;
    uint64_t header_block_id_2 = (2ULL << 32) | 2;
    uint64_t header_block_id_3 = (3ULL << 32) | 3;
    uint64_t header_block_id_4 = (4ULL << 32) | 4;
    
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_1, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_2, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_3, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_4, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);
    
    // Send multiple frames
    EXPECT_TRUE(sender->SendInsertCountIncrement(10));  // Clears all and calls callbacks
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 4);
    
    // Add back and send specific acks/cancels
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_1, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_2, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id_3, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 3);
    
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_1));  // Acks and calls callback
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 2);
    EXPECT_EQ(receiver->GetRetryCount(), 5);
    
    EXPECT_TRUE(sender->SendStreamCancel(header_block_id_2));  // Removes without callback
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    EXPECT_EQ(receiver->GetRetryCount(), 5);
    
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_3));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 6);
    
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// Test with large stream ID and section number
TEST(QpackDecoderStreamTest, LargeValuesTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);
    
    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    
    // Test with large values (varint encoding)
    uint64_t stream_id = 0xFFFFFF;  // Large stream ID
    uint64_t section_number = 0xFFFFFF;  // Large section number
    uint64_t header_block_id = (stream_id << 32) | section_number;
    
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(header_block_id, 
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    
    EXPECT_TRUE(sender->SendSectionAck(header_block_id));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 1);
    
    // Test with large delta
    uint64_t large_delta = 100000;
    EXPECT_TRUE(sender->SendInsertCountIncrement(large_delta));
    EXPECT_EQ(sender->GetErrorCode(), 0);
}

}  // namespace
}  // namespace http3
}  // namespace quicx

