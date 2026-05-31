#include <gtest/gtest.h>
#include <memory>

#include "http3/stream/type.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/stream/unidentified_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockSenderConnection {
public:
    MockSenderConnection(std::shared_ptr<IQuicSendStream> stream) 
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
    MockReceiverConnection(std::shared_ptr<IQuicRecvStream> stream) 
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
                                std::shared_ptr<IQuicRecvStream> stream,
                                std::shared_ptr<IBufferRead> remaining_data) {
        // Verify it's a QPACK decoder stream
        EXPECT_EQ(stream_type, static_cast<uint64_t>(StreamType::kQpackDecoder));
        
        // Create the actual QPACK decoder receiver stream
        receiver_stream_ = std::make_shared<QpackDecoderReceiverStream>(stream,
            blocked_registry_,
            std::bind(&MockReceiverConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
        
        // Feed remaining data to the decoder stream
        if (remaining_data && remaining_data->GetDataLength() > 0) {
            receiver_stream_->OnData(remaining_data, false, 0);
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

// Test sending and receiving Insert Count Increment frame.
//
// RFC 9204 §4.4.3: Insert Count Increment is sent by the *peer's decoder*
// to inform our *encoder* that the peer has applied N additional inserts.
// On the receiver side (this side, our local decoder receiver), receiving
// IIC must be advisory only — it MUST NOT touch the local blocked
// registry, because that registry tracks header-block sections waiting
// for *our* dynamic table to grow (via the encoder stream), not the
// peer's.  See bug fix in QpackDecoderReceiverStream::ParseDecoderFrames.
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
    
    // Send Insert Count Increment.  This frame is purely advisory state
    // for our encoder bookkeeping; it MUST NOT clear our local decoder's
    // blocked registry nor invoke any retry callbacks.
    uint64_t delta = 5;
    EXPECT_TRUE(sender->SendInsertCountIncrement(delta));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    
    // After Insert Count Increment, the blocked registry must be untouched.
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 2);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
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
    
    // IIC MUST NOT touch the local blocked registry.
    EXPECT_TRUE(sender->SendInsertCountIncrement(10));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
    
    // Each Section Ack acks the earliest outstanding section for the given
    // stream id (RFC 9204 §4.4.1) — so it removes exactly one entry and
    // invokes exactly one callback.
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_1));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 3);
    EXPECT_EQ(receiver->GetRetryCount(), 1);
    
    // Stream Cancellation removes one entry without calling its callback
    // (RFC 9204 §4.4.2).
    EXPECT_TRUE(sender->SendStreamCancel(header_block_id_2));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 2);
    EXPECT_EQ(receiver->GetRetryCount(), 1);
    
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_3));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    EXPECT_EQ(receiver->GetRetryCount(), 2);
    
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_4));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 3);
    
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

// PROBE: Insert Count Increment with delta=0 — RFC 9204 §4.4.3 encodes the
// delta as a 6-prefix integer; technically zero is a no-op but legal on the
// wire.  Our parser must not error and must not touch the blocked registry.
TEST(QpackDecoderStreamTest, InsertCountIncrementZeroDeltaTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);

    uint64_t key = (1ULL << 32) | 1;
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(key,
        std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);

    EXPECT_TRUE(sender->SendInsertCountIncrement(0));
    EXPECT_EQ(sender->GetErrorCode(), 0);

    // delta=0 still must not invoke retry callbacks nor clear the registry.
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
    EXPECT_EQ(receiver->GetErrorCode(), 0);
}

// PROBE: Section Ack on a stream id with NO outstanding section is a
// peer-side bookkeeping error per RFC 9204 §4.4.1.  Our receiver must not
// crash; it currently silently ignores it (logs internally).
TEST(QpackDecoderStreamTest, SectionAckUnknownStreamTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);

    // No prior Add — registry is empty.
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);

    uint64_t bogus = (99ULL << 32) | 1;
    EXPECT_TRUE(sender->SendSectionAck(bogus));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
}

// PROBE: Stream Cancellation on a stream id with NO outstanding section
// must also be a no-op without crashing.
TEST(QpackDecoderStreamTest, StreamCancelUnknownStreamTest) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);

    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);

    uint64_t bogus = (99ULL << 32) | 1;
    EXPECT_TRUE(sender->SendStreamCancel(bogus));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 0);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
}

}  // namespace
}  // namespace http3
}  // namespace quicx

