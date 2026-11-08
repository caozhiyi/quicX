#include <memory>

#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_decoder.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/stream/type.h"
#include "http3/stream/unidentified_stream.h"

#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

class MockSenderConnection {
public:
    MockSenderConnection(std::shared_ptr<IQuicSendStream> stream):
        error_code_(0) {
        sender_stream_ = std::make_shared<QpackDecoderSenderStream>(
            stream, std::bind(&MockSenderConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~MockSenderConnection() {}

    bool SendSectionAck(uint64_t header_block_id) { return sender_stream_->SendSectionAck(header_block_id); }

    bool SendStreamCancel(uint64_t header_block_id) { return sender_stream_->SendStreamCancel(header_block_id); }

    bool SendInsertCountIncrement(uint64_t delta) { return sender_stream_->SendInsertCountIncrement(delta); }

    void ErrorHandle(uint64_t /*stream_id*/, uint32_t error_code) { error_code_ = error_code; }

    uint32_t GetErrorCode() { return error_code_; }

private:
    uint32_t error_code_;
    std::shared_ptr<QpackDecoderSenderStream> sender_stream_;
};

class MockReceiverConnection {
public:
    MockReceiverConnection(std::shared_ptr<IQuicRecvStream> stream):
        error_code_(0),
        retry_count_(0) {
        blocked_registry_ = std::make_shared<QpackBlockedRegistry>();

        // Start with UnidentifiedStream to read stream type
        unidentified_stream_ = std::make_shared<UnidentifiedStream>(stream,
            std::bind(&MockReceiverConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockReceiverConnection::OnStreamTypeIdentified, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
    }
    ~MockReceiverConnection() {}

    void OnStreamTypeIdentified(
        uint64_t stream_type, std::shared_ptr<IQuicRecvStream> stream, std::shared_ptr<IBufferRead> remaining_data) {
        // Verify it's a QPACK decoder stream
        EXPECT_EQ(stream_type, static_cast<uint64_t>(StreamType::kQpackDecoder));

        // Create the actual QPACK decoder receiver stream
        receiver_stream_ = std::make_shared<QpackDecoderReceiverStream>(stream, local_encoder_, blocked_registry_,
            std::bind(&MockReceiverConnection::ErrorHandle, this, std::placeholders::_1, std::placeholders::_2));

        // Feed remaining data to the decoder stream
        if (remaining_data && remaining_data->GetDataLength() > 0) {
            receiver_stream_->OnData(remaining_data, false, 0);
        }
    }

    void ErrorHandle(uint64_t /*stream_id*/, uint32_t error_code) { error_code_ = error_code; }

    void RetryCallback() { retry_count_++; }

    uint32_t GetErrorCode() { return error_code_; }
    uint32_t GetRetryCount() { return retry_count_; }
    std::shared_ptr<QpackBlockedRegistry> GetBlockedRegistry() { return blocked_registry_; }
    std::shared_ptr<QpackEncoder> GetLocalEncoder() { return local_encoder_; }

private:
    uint32_t error_code_;
    uint32_t retry_count_;

    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::shared_ptr<QpackEncoder> local_encoder_{std::make_shared<QpackEncoder>()};
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
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);

    EXPECT_TRUE(sender->SendSectionAck(header_block_id));
    EXPECT_EQ(sender->GetErrorCode(), 0);

    // RFC 9204 4.4.1: a Section Acknowledgment comes from the PEER's decoder
    // and concerns header blocks produced by OUR encoder. Our decoder's blocked
    // sections (waiting for OUR table to grow via the encoder stream) are
    // unrelated, so the registry must be left exactly as it was -- the same
    // rule Insert Count Increment already follows. It used to retry-and-erase
    // the entry, which on a bidirectional stream could drop a response section.
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
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

    // A blocked decoder section of OURS on stream 7
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);

    EXPECT_TRUE(sender->SendStreamCancel(header_block_id));
    EXPECT_EQ(sender->GetErrorCode(), 0);

    // RFC 9204 4.4.2: the peer's decoder abandoned ITS view of the stream, which
    // says nothing about our decoder's section on the same stream id. The
    // registry must keep it.
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);
    EXPECT_EQ(receiver->GetRetryCount(), 0);
}

// Puts |enc| in the state "one header block on |stream_id| is outstanding": two
// acknowledged entries, then a block that references one of them.
static void MakeOutstandingSection(QpackEncoder& enc, uint64_t stream_id) {
    using Fields = std::vector<std::pair<std::string, std::string>>;
    auto buf = [] {
        return std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(2048));
    };
    enc.SetLocalMaxTableCapacity(100);
    enc.SetPeerMaxTableCapacity(100);
    enc.SetDynamicTableEnabled(true);
    enc.SetInstructionSender([](const Fields&) {});
    Fields a = {{"x-a", "aaaa"}};
    Fields b = {{"x-b", "bbbb"}};
    ASSERT_TRUE(enc.Encode(a, buf()));
    ASSERT_TRUE(enc.Encode(b, buf()));
    enc.OnPeerInsertCountIncrement(2);
    ASSERT_TRUE(enc.Encode(a, buf(), stream_id));
    ASSERT_EQ(enc.GetOutstandingSectionCount(), 1u);
}

// WIRING: feedback arriving on the decoder stream is for OUR ENCODER's blocks.
// QpackEncoder::OnPeerSectionAck has its own tests; this one proves the
// receiver stream actually delivers the frame to it (it used to hand it to the
// decoder's blocked registry, so the encoder never heard a single ack).
TEST(QpackDecoderStreamTest, SectionAckReachesTheLocalEncoder) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    MakeOutstandingSection(*receiver->GetLocalEncoder(), /*stream_id=*/5);

    EXPECT_TRUE(sender->SendSectionAck(5ULL << 32));  // packed key: (stream_id << 32) | section
    EXPECT_EQ(receiver->GetLocalEncoder()->GetOutstandingSectionCount(), 0u);
    EXPECT_EQ(receiver->GetErrorCode(), 0u);
}

TEST(QpackDecoderStreamTest, StreamCancellationReachesTheLocalEncoder) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    MakeOutstandingSection(*receiver->GetLocalEncoder(), /*stream_id=*/5);

    EXPECT_TRUE(sender->SendStreamCancel(5ULL << 32));
    EXPECT_EQ(receiver->GetLocalEncoder()->GetOutstandingSectionCount(), 0u);
}

// END-TO-END on the wire: the decoder-side CancelStreamFeedback(stream_id) must
// reach the peer's encoder naming THAT stream. The sender stream takes a packed
// header-block key ((stream_id << 32) | section) and puts only its high half on
// the wire; CancelStreamFeedback used to hand it the bare stream id, so every
// Stream Cancellation went out for stream 0 no matter which stream was abandoned.
TEST(QpackDecoderStreamTest, CancelStreamFeedbackNamesTheRealStreamOnTheWire) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    MakeOutstandingSection(*receiver->GetLocalEncoder(), /*stream_id=*/13);

    // The local DECODER of the other endpoint, wired like IConnection wires it.
    QpackDecoder peer_decoder;
    peer_decoder.SetDecoderFeedbackSender([&sender](uint8_t type, uint64_t value) {
        if (type == static_cast<uint8_t>(QpackDecoderInstrType::kStreamCancellation)) {
            sender->SendStreamCancel(value);
        }
    });
    peer_decoder.CancelStreamFeedback(13);

    EXPECT_EQ(receiver->GetLocalEncoder()->GetOutstandingSectionCount(), 0u)
        << "the cancellation must name stream 13, not stream 0";
}

TEST(QpackDecoderStreamTest, FeedbackForAnotherStreamLeavesTheSectionOutstanding) {
    auto mock_send_stream = std::make_shared<quic::MockQuicStream>();
    auto mock_recv_stream = std::make_shared<quic::MockQuicStream>();
    mock_recv_stream->SetPeer(mock_send_stream);
    mock_send_stream->SetPeer(mock_recv_stream);

    auto sender = std::make_shared<MockSenderConnection>(mock_send_stream);
    auto receiver = std::make_shared<MockReceiverConnection>(mock_recv_stream);
    MakeOutstandingSection(*receiver->GetLocalEncoder(), /*stream_id=*/5);

    EXPECT_TRUE(sender->SendSectionAck(9ULL << 32));     // no outstanding section on stream 9
    EXPECT_TRUE(sender->SendStreamCancel(13ULL << 32));  // nor on 13
    EXPECT_EQ(receiver->GetLocalEncoder()->GetOutstandingSectionCount(), 1u);
    EXPECT_EQ(receiver->GetErrorCode(), 0u) << "an unmatched ack is logged and ignored, not a connection error";
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

    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id_1, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id_2, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
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

    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id_1, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id_2, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id_3, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id_4, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);

    // IIC MUST NOT touch the local blocked registry.
    EXPECT_TRUE(sender->SendInsertCountIncrement(10));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);
    EXPECT_EQ(receiver->GetRetryCount(), 0);

    // Section Ack / Stream Cancellation are feedback for OUR encoder's blocks
    // (RFC 9204 4.4.1 / 4.4.2); none of them may touch our decoder's blocked
    // registry, in any order and in any mix.
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_1));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);
    EXPECT_EQ(receiver->GetRetryCount(), 0);

    EXPECT_TRUE(sender->SendStreamCancel(header_block_id_2));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);
    EXPECT_EQ(receiver->GetRetryCount(), 0);

    EXPECT_TRUE(sender->SendSectionAck(header_block_id_3));
    EXPECT_TRUE(sender->SendSectionAck(header_block_id_4));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 4);
    EXPECT_EQ(receiver->GetRetryCount(), 0);

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
    uint64_t stream_id = 0xFFFFFF;       // Large stream ID
    uint64_t section_number = 0xFFFFFF;  // Large section number
    uint64_t header_block_id = (stream_id << 32) | section_number;

    EXPECT_TRUE(receiver->GetBlockedRegistry()->Add(
        header_block_id, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);

    EXPECT_TRUE(sender->SendSectionAck(header_block_id));
    EXPECT_EQ(sender->GetErrorCode(), 0);
    EXPECT_EQ(receiver->GetBlockedRegistry()->GetBlockedCount(), 1);  // untouched, see SectionAckTest
    EXPECT_EQ(receiver->GetRetryCount(), 0);

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
    EXPECT_TRUE(
        receiver->GetBlockedRegistry()->Add(key, std::bind(&MockReceiverConnection::RetryCallback, receiver.get())));
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
