// QPACK head-of-line blocking + inline DATA frame regression test.
// ----------------------------------------------------------------------------
// Background
// ----------
// Before the fix, ReqRespBaseStream::OnData() iterated through *every* frame
// returned by the FrameDecoder in one shot and dispatched them in order via
// HandleFrame(). When a HEADERS frame's Required Insert Count exceeded the
// decoder's current Insert Count (RFC 9204 §2.1.4), HandleHeaders() returned
// without populating headers_/response_, but the loop happily continued to
// the next frame. If that next frame was a DATA frame (the very common case
// of "headers + body delivered in one network burst"), HandleData() would
// dereference a still-null response_ / request_ object and crash.
//
// The fix introduces:
//   - is_currently_blocked_ : a gate flipped by HandleHeaders() on RIC miss
//   - pending_blocked_frames_ : an in-order queue of frames that arrived
//     behind the blocked HEADERS, including any future OnData batches
//   - DrainPendingFrames() : invoked from the blocked-registry retry once
//     the QPACK decoder caches enough inserts to decode the head HEADERS,
//     replays queued frames in order, and surfaces the deferred FIN bit.
//
// What this test exercises
// ------------------------
// 1. A *single* OnData batch is delivered to a client-side RequestStream.
//    The batch contains, in order: HEADERS (RIC=1) + DATA + FIN.
// 2. The decoder's dynamic table has Insert Count = 0, so HEADERS is
//    blocked.  Without the fix, dispatching DATA next dereferences
//    response_ == nullptr → crash.  With the fix, the DATA frame is
//    parked and the response_handler is NOT invoked yet.
// 3. We then feed the matching encoder Insert instruction directly into
//    the decoder (simulating QPACK encoder stream delivery) and call
//    blocked_registry_->NotifyAll().
// 4. The retry callback decodes the HEADERS, invokes HandleHeaders()
//    which constructs response_, then DrainPendingFrames() replays the
//    parked DATA frame against the now-non-null response_, and the FIN
//    bit closes out the stream.  The application's response_handler must
//    fire with the correct status, headers and body — proving the
//    end-to-end path works under blocking.
// ----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include <quicx/http3/if_response.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/request_stream.h"

#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

// Build a HEADERS frame whose encoded payload references the dynamic table
// at absolute index 0 (RIC = 1, Base = 0). The encoder driver inserts the
// (name, value) pair into ITS OWN dynamic table so the prefix encoding
// (max_entries / RIC math) is correct, but we deliberately *do not* feed
// that insert to the receiver-side decoder yet — that's what makes the
// header block QPACK-blocked on arrival.
std::shared_ptr<common::IBuffer> BuildBlockedHeadersFrame(
    QpackEncoder& driver,
    const std::string& name,
    const std::string& value) {
    // Drive the encoder so its internal dynamic table has 1 entry, which
    // makes WriteHeaderPrefix(ric=1, base=0) produce a valid wire prefix.
    {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(256);
        auto instr_buf = std::make_shared<common::SingleBlockBuffer>(chunk);
        std::vector<std::pair<std::string, std::string>> ins{{name, value}};
        EXPECT_TRUE(driver.EncodeEncoderInstructions(ins, instr_buf));
        // Self-feed so driver.dynamic_table_ now has the entry at abs=0
        EXPECT_TRUE(driver.DecodeEncoderInstructions(instr_buf));
    }

    // Build the HEADERS payload: prefix(ric=1, base=0) followed by a
    // PostBaseIndexed reference to absolute index 0 (post_base_index = 0).
    // Wire byte for "Indexed Header Field with Post-Base Index" is
    // 0001xxxx — see QpackHeaderPattern::kPostBaseIndexed in qpack_encoder.cpp.
    auto payload_chunk = std::make_shared<common::StandaloneBufferChunk>(64);
    auto payload = std::make_shared<common::SingleBlockBuffer>(payload_chunk);
    driver.WriteHeaderPrefix(payload, /*ric=*/1, /*base=*/0);
    uint8_t post_base = 0x10;  // pattern 0001 0000, post_base_index=0
    payload->Write(&post_base, 1);

    // Wrap into a HeadersFrame and serialise to a wire buffer.
    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(payload);
    auto frame_chunk = std::make_shared<common::StandaloneBufferChunk>(128);
    auto frame_buf = std::make_shared<common::SingleBlockBuffer>(frame_chunk);
    EXPECT_TRUE(headers_frame.Encode(frame_buf));
    return frame_buf;
}

// Serialise a DATA frame carrying |body| into a fresh wire buffer.
std::shared_ptr<common::IBuffer> BuildDataFrame(const std::string& body) {
    auto data_chunk = std::make_shared<common::StandaloneBufferChunk>(
        static_cast<uint32_t>(body.size() + 16));
    auto data_buf = std::make_shared<common::SingleBlockBuffer>(data_chunk);
    data_buf->Write(reinterpret_cast<const uint8_t*>(body.data()),
                    static_cast<uint32_t>(body.size()));

    DataFrame data_frame;
    data_frame.SetData(data_buf);

    auto frame_chunk = std::make_shared<common::StandaloneBufferChunk>(
        static_cast<uint32_t>(body.size() + 32));
    auto frame_buf = std::make_shared<common::SingleBlockBuffer>(frame_chunk);
    EXPECT_TRUE(data_frame.Encode(frame_buf));
    return frame_buf;
}

// Concatenate two wire buffers into a single buffer — that's what the QUIC
// layer would deliver when both frames arrive in the same STREAM packet.
std::shared_ptr<common::IBuffer> Concat(
    const std::shared_ptr<common::IBuffer>& a,
    const std::shared_ptr<common::IBuffer>& b) {
    uint32_t la = a->GetDataLength();
    uint32_t lb = b->GetDataLength();
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(la + lb);
    auto out = std::make_shared<common::SingleBlockBuffer>(chunk);
    a->VisitData([&](uint8_t* d, uint32_t l) { out->Write(d, l); return true; });
    b->VisitData([&](uint8_t* d, uint32_t l) { out->Write(d, l); return true; });
    return out;
}

class QpackBlockedInlineDataTest: public ::testing::Test {
protected:
    void SetUp() override {
        // Stream that the production code "owns" — the RequestStream installs
        // its read callback here and treats it as the local QUIC bidi stream.
        stream_ = std::make_shared<quic::MockQuicStream>(0);

        // Receiver-side decoder. Its dynamic table starts at InsertCount=0
        // so any HEADERS with RIC>=1 will block.
        decoder_ = std::make_shared<QpackEncoder>();
        decoder_->SetMaxTableCapacity(4096);
        decoder_->SetDynamicTableEnabled(true);

        // Encoder we don't actually use for receiving — RequestStream needs
        // a non-null encoder so SendRequest could work. Distinct instance
        // from decoder_ to mirror the real per-direction layout (RFC 9204
        // §3.2: client-encoder ↔ server-decoder for requests).
        encoder_ = std::make_shared<QpackEncoder>();
        encoder_->SetMaxTableCapacity(4096);
        encoder_->SetDynamicTableEnabled(true);

        blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
        blocked_registry_->SetMaxBlockedStreams(16);

        request_stream_ = std::make_shared<RequestStream>(
            encoder_, decoder_, blocked_registry_, stream_,
            [this](std::shared_ptr<IResponse> resp, uint32_t err) {
                response_ = resp;
                response_error_ = err;
                ++response_invocations_;
            },
            [this](uint64_t /*stream_id*/, uint32_t err) {
                error_code_ = err;
            },
            [](std::unordered_map<std::string, std::string>&, uint64_t) {});
        request_stream_->Init();
    }

    std::shared_ptr<quic::MockQuicStream> stream_;
    std::shared_ptr<QpackEncoder> encoder_;   // outgoing; unused by reads
    std::shared_ptr<QpackEncoder> decoder_;   // incoming; starts empty
    std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    std::shared_ptr<RequestStream> request_stream_;

    std::shared_ptr<IResponse> response_;
    uint32_t response_error_{0};
    int response_invocations_{0};
    uint32_t error_code_{0};
};

// Regression: HEADERS (RIC=1, blocked) followed by DATA in a SINGLE OnData
// batch must NOT crash and must NOT prematurely deliver the response. After
// the encoder Insert instruction is fed to the decoder and the registry is
// notified, the queued DATA frame must replay against the freshly-built
// response_ object and the application handler must observe the full body.
TEST_F(QpackBlockedInlineDataTest, BlockedHeadersFollowedByDataInSameBatch) {
    // The HEADERS block carries :status (so HandleHeaders() can build a
    // valid Response), content-length (so RequestStream waits for the
    // whole body before invoking the handler — keeps the assertion of a
    // single response_handler invocation deterministic), and a custom
    // header that is not in the static table (forces a dynamic-table
    // insert and therefore RIC=1 — the source of blocking).
    QpackEncoder driver;
    driver.SetMaxTableCapacity(4096);
    driver.SetDynamicTableEnabled(true);

    const std::string body = "the-quick-brown-fox-jumps-over-the-lazy-dog";

    std::unordered_map<std::string, std::string> headers_in{
        {":status", "200"},
        {"content-length", std::to_string(body.size())},
        {"x-trace", "abc-123"},  // not in static table → goes to dynamic table
    };
    // Hook the driver's instruction sender so we capture the encoder
    // instructions it produces (we'll feed them to decoder_ later).
    std::vector<std::pair<std::string, std::string>> captured_inserts;
    driver.SetInstructionSender(
        [&](const std::vector<std::pair<std::string, std::string>>& ins) {
            for (const auto& p : ins) captured_inserts.push_back(p);
        });

    auto encoded_chunk = std::make_shared<common::StandaloneBufferChunk>(256);
    auto encoded_payload = std::make_shared<common::SingleBlockBuffer>(encoded_chunk);
    ASSERT_TRUE(driver.Encode(headers_in, encoded_payload));
    ASSERT_FALSE(captured_inserts.empty()) << "driver did not insert into dyn table";

    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(encoded_payload);
    auto headers_wire_chunk = std::make_shared<common::StandaloneBufferChunk>(384);
    auto headers_wire = std::make_shared<common::SingleBlockBuffer>(headers_wire_chunk);
    ASSERT_TRUE(headers_frame.Encode(headers_wire));

    // Body content — must arrive intact at the application after replay.
    auto data_wire = BuildDataFrame(body);

    // Concatenate to mimic a single QUIC STREAM frame carrying both.
    auto combined = Concat(headers_wire, data_wire);

    // Sanity: registry should be empty before we deliver.
    EXPECT_EQ(blocked_registry_->GetBlockedCount(), 0u);

    // Deliver as a single OnData batch with FIN=true.
    stream_->SimulateRead(combined, /*is_last=*/true);

    // The HEADERS must have blocked. Therefore:
    //   - response handler MUST NOT have fired yet
    //   - registry MUST hold exactly one pending entry
    //   - error_code_ MUST be 0 (no decompression-failed escape hatch)
    EXPECT_EQ(response_invocations_, 0)
        << "response handler fired before HEADERS unblocked";
    EXPECT_EQ(blocked_registry_->GetBlockedCount(), 1u)
        << "blocked HEADERS not registered for retry";
    EXPECT_EQ(error_code_, 0u)
        << "stream errored out instead of parking";

    // Now feed the missing dynamic-table inserts to decoder_, then notify.
    {
        QpackEncoder instr_emitter;
        std::vector<std::pair<std::string, std::string>> ins = captured_inserts;
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(256);
        auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
        ASSERT_TRUE(instr_emitter.EncodeEncoderInstructions(ins, buf));
        ASSERT_TRUE(decoder_->DecodeEncoderInstructions(buf));
    }
    EXPECT_GT(decoder_->GetInsertCount(), 0u);

    // Trigger the retry path. The blocked HEADERS retry callback should:
    //   1. Re-decode the field section successfully
    //   2. Build response_ and emit headers
    //   3. DrainPendingFrames() → replay DATA against the now-built response_
    //   4. Surface FIN, completing the response
    blocked_registry_->NotifyAll();

    EXPECT_EQ(response_invocations_, 1)
        << "response handler must fire exactly once after unblocking";
    ASSERT_NE(response_, nullptr);
    EXPECT_EQ(response_error_, 0u);
    EXPECT_EQ(response_->GetStatusCode(), 200);

    std::string trace;
    EXPECT_TRUE(response_->GetHeader("x-trace", trace));
    EXPECT_EQ(trace, "abc-123");

    EXPECT_EQ(response_->GetBodyAsString(), body)
        << "DATA frame parked behind blocked HEADERS was not replayed verbatim";

    // Registry must be drained by now.
    EXPECT_EQ(blocked_registry_->GetBlockedCount(), 0u);
}

// Regression: a second OnData batch arriving WHILE the stream is still
// QPACK-blocked must also be parked, not dispatched. This guards the
// "is_currently_blocked_ in OnData()" path.
TEST_F(QpackBlockedInlineDataTest, AdditionalDataWhileBlockedIsAlsoParked) {
    QpackEncoder driver;
    driver.SetMaxTableCapacity(4096);
    driver.SetDynamicTableEnabled(true);
    std::vector<std::pair<std::string, std::string>> captured_inserts;
    driver.SetInstructionSender(
        [&](const std::vector<std::pair<std::string, std::string>>& ins) {
            for (const auto& p : ins) captured_inserts.push_back(p);
        });

    const std::string body = "second-batch-body";

    std::unordered_map<std::string, std::string> headers_in{
        {":status", "200"},
        {"content-length", std::to_string(body.size())},
        {"x-trace", "v"},
    };
    auto enc_chunk = std::make_shared<common::StandaloneBufferChunk>(256);
    auto enc_payload = std::make_shared<common::SingleBlockBuffer>(enc_chunk);
    ASSERT_TRUE(driver.Encode(headers_in, enc_payload));

    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(enc_payload);
    auto h_chunk = std::make_shared<common::StandaloneBufferChunk>(384);
    auto h_wire = std::make_shared<common::SingleBlockBuffer>(h_chunk);
    ASSERT_TRUE(headers_frame.Encode(h_wire));

    // Step 1: deliver only the HEADERS, no FIN. Stream is now blocked.
    stream_->SimulateRead(h_wire, /*is_last=*/false);
    EXPECT_EQ(response_invocations_, 0);
    EXPECT_EQ(blocked_registry_->GetBlockedCount(), 1u);

    // Step 2: deliver a DATA frame in a *separate* OnData batch with FIN.
    // OnData must observe is_currently_blocked_ and queue these bytes
    // rather than dispatching.
    auto d_wire = BuildDataFrame(body);
    stream_->SimulateRead(d_wire, /*is_last=*/true);
    EXPECT_EQ(response_invocations_, 0)
        << "response handler fired before HEADERS unblocked";
    EXPECT_EQ(error_code_, 0u);

    // Step 3: feed inserts and notify — replay must include the DATA from
    // batch 2 AND honour its FIN bit.
    {
        QpackEncoder e;
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(256);
        auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
        ASSERT_TRUE(e.EncodeEncoderInstructions(captured_inserts, buf));
        ASSERT_TRUE(decoder_->DecodeEncoderInstructions(buf));
    }
    blocked_registry_->NotifyAll();

    EXPECT_EQ(response_invocations_, 1);
    ASSERT_NE(response_, nullptr);
    EXPECT_EQ(response_->GetStatusCode(), 200);
    EXPECT_EQ(response_->GetBodyAsString(), body);
}

}  // namespace
}  // namespace http3
}  // namespace quicx
