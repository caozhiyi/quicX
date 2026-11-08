// P1-H5a / H6c-g compliance regression tests.
// ----------------------------------------------------------------------------
// Covers the stage-4 message-layer fixes:
//   H5a  QPACK encoder/decoder instruction streams keep an unterminated tail
//        across OnData boundaries (byte-stream semantics) instead of parsing
//        it from a wrong offset.
//   H6c  content-length validation: conflicting duplicates, "0 + DATA",
//        FIN short of the declared length.
//   H6d  trailers append to the request without re-routing; pseudo-headers
//        in trailers are rejected.
//   H6e  1xx informational responses do not complete the request; the final
//        response does. Response body cap aborts with EXCESSIVE_LOAD.
//   H6f  PushReceiverStream rejects push ids above MAX_PUSH_ID / duplicates
//        (H3_ID_ERROR).
//   H6g  max_request_body_size_ is per server instance (was process-wide
//        static).
// ----------------------------------------------------------------------------

#include <cstring>
#include <memory>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/http/error.h"
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/qpack/blocked_registry.h"
#include "http3/qpack/qpack_decoder.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/push_receiver_stream.h"
#include "http3/stream/qpack_decoder_receiver_stream.h"
#include "http3/stream/qpack_encoder_receiver_stream.h"
#include "http3/stream/request_stream.h"
#include "http3/stream/response_stream.h"

#include "test/unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

// ==================== wire helpers ====================

std::shared_ptr<common::IBuffer> FreshBuffer(uint32_t size) {
    return std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(size));
}

std::shared_ptr<common::IBuffer> BuildDataFrame(const std::string& body) {
    DataFrame data_frame;
    auto payload = FreshBuffer(static_cast<uint32_t>(body.size()) + 16);
    payload->Write(reinterpret_cast<const uint8_t*>(body.data()), static_cast<uint32_t>(body.size()));
    data_frame.SetData(payload);
    auto wire = FreshBuffer(static_cast<uint32_t>(body.size()) + 64);
    if (!data_frame.Encode(wire)) {
        return nullptr;
    }
    return wire;
}

std::shared_ptr<common::IBuffer> BuildHeadersFrame(QpackEncoder& driver, const HttpFields& fields) {
    auto encoded_payload = FreshBuffer(1024);
    if (!driver.Encode(fields, encoded_payload, /*stream_id=*/0)) {
        return nullptr;
    }
    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(encoded_payload);
    auto wire = FreshBuffer(1280);
    if (!headers_frame.Encode(wire)) {
        return nullptr;
    }
    return wire;
}

std::shared_ptr<common::IBuffer> Concat(const std::shared_ptr<common::IBuffer>& a, const std::shared_ptr<common::IBuffer>& b) {
    auto out = FreshBuffer(a->GetDataLength() + b->GetDataLength() + 16);
    out->Write(a);
    out->Write(b);
    return out;
}

// ==================== H6c/H6d/H6g: server-side message handling ====================

class MessageComplianceTest: public testing::Test {
protected:
    void SetUp() override {
        qpack_encoder_ = std::make_shared<QpackEncoder>();
        qpack_decoder_ = std::make_shared<QpackDecoder>();
        mock_stream_1_ = std::make_shared<quic::MockQuicStream>(1);
        mock_stream_2_ = std::make_shared<quic::MockQuicStream>(2);
        mock_stream_2_->SetPeer(mock_stream_1_);
        mock_stream_1_->SetPeer(mock_stream_2_);

        client_connection_ = std::make_shared<MockClient>(qpack_encoder_, qpack_decoder_, mock_stream_1_);
        server_connection_ = std::make_shared<MockServer>(qpack_encoder_, qpack_decoder_, mock_stream_2_);
    }

    // Feed wire bytes directly to the server's ResponseStream (bypasses the
    // client so tests control exact field ordering, FIN timing, etc).
    void FeedServer(const std::shared_ptr<common::IBuffer>& wire, bool is_last) {
        mock_stream_2_->SimulateRead(wire, is_last, /*error=*/0);
    }

    class MockClient {
    public:
        MockClient(const std::shared_ptr<QpackEncoder>& enc, const std::shared_ptr<QpackDecoder>& dec,
            std::shared_ptr<quic::MockQuicStream> stream):
            error_code_(0),
            response_invocations_(0) {
            blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
            request_stream_ =
                std::make_shared<RequestStream>(enc, dec, blocked_registry_, stream,
                    [this](std::shared_ptr<IResponse> resp, uint32_t err) {
                        response_ = resp;
                        response_invocations_++;
                        error_code_ = err;
                    },
                    [this](uint64_t, uint32_t err) { error_code_ = err; },
                    [](HttpFields&, uint64_t) {});
            request_stream_->Init();
        }

        bool SendRequest(std::shared_ptr<IRequest> request) { return request_stream_->SendRequest(request); }

        uint32_t error_code_;
        int response_invocations_;
        std::shared_ptr<IResponse> response_;
        std::shared_ptr<RequestStream> request_stream_;
        std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    };

    class MockProcessor: public IHttpProcessor {
    public:
        MockProcessor(RouteConfig (*match)(void*), void* ctx, uint64_t body_cap):
            match_(match),
            ctx_(ctx),
            body_cap_(body_cap) {}
        RouteConfig MatchRoute(HttpMethod method, const std::string& path, std::shared_ptr<IRequest> request) override {
            return match_(ctx_);
        }
        bool BeforeHandlerProcess(std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) override { return true; }
        void AfterHandlerProcess(std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) override {}
        uint64_t GetMaxRequestBodySize() const override { return body_cap_; }

    private:
        RouteConfig (*match_)(void*);
        void* ctx_;
        uint64_t body_cap_;
    };

    class MockServer {
    public:
        MockServer(const std::shared_ptr<QpackEncoder>& enc, const std::shared_ptr<QpackDecoder>& dec,
            std::shared_ptr<quic::MockQuicStream> stream, uint64_t body_cap = 16ull * 1024 * 1024):
            error_code_(0),
            handler_invocations_(0),
            body_cap_(body_cap) {
            blocked_registry_ = std::make_shared<QpackBlockedRegistry>();
            processor_ = std::make_shared<Processor>(this);
            response_stream_ =
                std::make_shared<ResponseStream>(enc, dec, blocked_registry_, stream, processor_,
                    [](std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>) {},
                    [this](uint64_t, uint32_t err) { error_code_ = err; }, []() { return true; });
            response_stream_->Init();
            body_cap_ = body_cap;
        }

        class Processor: public IHttpProcessor {
        public:
            Processor(MockServer* server):
                server_(server) {}
            RouteConfig MatchRoute(HttpMethod, const std::string&, std::shared_ptr<IRequest>) override {
                return RouteConfig([server = server_](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
                    server->handler_invocations_++;
                    server->last_request_ = std::dynamic_pointer_cast<Request>(req);
                    if (server->user_handler_) {
                        server->user_handler_(req, resp);
                    } else {
                        resp->SetStatusCode(200);
                    }
                });
            }
            bool BeforeHandlerProcess(std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) override { return true; }
            void AfterHandlerProcess(std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) override {}
            uint64_t GetMaxRequestBodySize() const override { return server_->body_cap_; }

        private:
            MockServer* server_;
        };

        uint32_t error_code_;
        int handler_invocations_;
        http_handler user_handler_;
        std::shared_ptr<Request> last_request_;
        uint64_t body_cap_ = 16ull * 1024 * 1024;
        std::shared_ptr<IHttpProcessor> processor_;
        std::shared_ptr<ResponseStream> response_stream_;
        std::shared_ptr<QpackBlockedRegistry> blocked_registry_;
    };
    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<QpackDecoder> qpack_decoder_;
    std::shared_ptr<quic::MockQuicStream> mock_stream_1_;
    std::shared_ptr<quic::MockQuicStream> mock_stream_2_;
    std::shared_ptr<MockClient> client_connection_;
    std::shared_ptr<MockServer> server_connection_;
};

// ---- H6c: conflicting duplicate content-length is a smuggling vector ----
TEST_F(MessageComplianceTest, ConflictingContentLengthRejected) {
    HttpFields headers{
        {":method", "POST"},
        {":scheme", "https"},
        {":path", "/upload"},
        {":authority", "example.com"},
        {"content-length", "5"},
        {"content-length", "6"},
    };
    auto wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(wire, nullptr);

    FeedServer(wire, /*is_last=*/false);

    EXPECT_EQ(server_connection_->error_code_, kMessageError) << "mismatched content-length duplicates must error";
    EXPECT_EQ(server_connection_->handler_invocations_, 0) << "request must not reach routing";
}

// ---- H6c: "content-length: 0" followed by DATA is a smuggling vector ----
TEST_F(MessageComplianceTest, ContentLengthZeroWithDataRejected) {
    HttpFields headers{
        {":method", "GET"},
        {":scheme", "https"},
        {":path", "/x"},
        {":authority", "example.com"},
        {"content-length", "0"},
    };
    auto headers_wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(headers_wire, nullptr);
    FeedServer(headers_wire, /*is_last=*/false);
    ASSERT_EQ(server_connection_->error_code_, 0u);

    auto data_wire = BuildDataFrame("smuggled-body");
    ASSERT_NE(data_wire, nullptr);
    FeedServer(data_wire, /*is_last=*/false);

    EXPECT_EQ(server_connection_->error_code_, kMessageError) << "DATA after content-length: 0 must error";
}

// ---- H6c: FIN before the declared body length arrived ----
TEST_F(MessageComplianceTest, FinShortOfDeclaredBodyRejected) {
    HttpFields headers{
        {":method", "POST"},
        {":scheme", "https"},
        {":path", "/upload"},
        {":authority", "example.com"},
        {"content-length", "100"},
    };
    auto wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(wire, nullptr);
    // headers + FIN, no DATA at all → declared 100 bytes, got 0.
    FeedServer(wire, /*is_last=*/true);

    EXPECT_EQ(server_connection_->error_code_, kMessageError) << "FIN short of declared content-length must error";
    EXPECT_EQ(server_connection_->handler_invocations_, 0);
}

// ---- H6d: trailers append, never re-route ----
TEST_F(MessageComplianceTest, TrailersAppendWithoutRerouting) {
    server_connection_->user_handler_ = [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
        resp->SetStatusCode(200);
    };

    HttpFields headers{
        {":method", "POST"},
        {":scheme", "https"},
        {":path", "/upload"},
        {":authority", "example.com"},
        {"content-length", "5"},
    };
    auto headers_wire = BuildHeadersFrame(*qpack_encoder_, headers);
    auto data_wire = BuildDataFrame("12345");
    ASSERT_NE(headers_wire, nullptr);
    ASSERT_NE(data_wire, nullptr);

    // HEADERS + DATA(complete) without FIN.
    FeedServer(Concat(headers_wire, data_wire), /*is_last=*/false);
    ASSERT_EQ(server_connection_->handler_invocations_, 1) << "handler runs exactly once for the request";

    // Now a trailing field section (trailers) arrives.
    HttpFields trailers{{"x-checksum", "deadbeef"}};
    auto trailers_wire = BuildHeadersFrame(*qpack_encoder_, trailers);
    ASSERT_NE(trailers_wire, nullptr);
    FeedServer(trailers_wire, /*is_last=*/true);

    EXPECT_EQ(server_connection_->handler_invocations_, 1) << "trailers must NOT re-run routing/handler";
    EXPECT_EQ(server_connection_->error_code_, 0u) << "valid trailers are not an error";
    ASSERT_NE(server_connection_->last_request_, nullptr);
    EXPECT_EQ(server_connection_->last_request_->GetTrailers().size(), 1u);
    EXPECT_EQ(server_connection_->last_request_->GetTrailers()[0].first, "x-checksum");
    EXPECT_EQ(server_connection_->last_request_->GetTrailers()[0].second, "deadbeef");
}

// ---- H6d: pseudo-header in trailers is a protocol error ----
TEST_F(MessageComplianceTest, PseudoHeaderInTrailersRejected) {
    HttpFields headers{
        {":method", "GET"},
        {":scheme", "https"},
        {":path", "/x"},
        {":authority", "example.com"},
    };
    auto headers_wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(headers_wire, nullptr);
    FeedServer(headers_wire, /*is_last=*/false);

    HttpFields bad_trailers{{":status", "200"}};
    auto trailers_wire = BuildHeadersFrame(*qpack_encoder_, bad_trailers);
    ASSERT_NE(trailers_wire, nullptr);
    FeedServer(trailers_wire, /*is_last=*/false);

    EXPECT_EQ(server_connection_->error_code_, kMessageError);
}

// ---- H6e: 1xx is informational; the final response completes the request ----
TEST_F(MessageComplianceTest, Informational1xxDoesNotCompleteRequest) {
    // Feed a 100 response HEADERS to the client's RequestStream.
    HttpFields informational{{":status", "100"}};
    auto info_wire = BuildHeadersFrame(*qpack_encoder_, informational);
    ASSERT_NE(info_wire, nullptr);
    mock_stream_1_->SimulateRead(info_wire, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(client_connection_->response_invocations_, 0) << "1xx must not invoke the response handler";
    EXPECT_EQ(client_connection_->error_code_, 0u) << "1xx is not an error";

    // The final 200 (body-less, CL:0) completes the request.
    HttpFields final{{":status", "200"}, {"content-length", "0"}};
    auto final_wire = BuildHeadersFrame(*qpack_encoder_, final);
    ASSERT_NE(final_wire, nullptr);
    mock_stream_1_->SimulateRead(final_wire, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(client_connection_->response_invocations_, 1) << "final response completes the request";
    ASSERT_NE(client_connection_->response_, nullptr);
    EXPECT_EQ(client_connection_->response_->GetStatusCode(), 200);
}

// ---- H6e: 101 Switching Protocols is invalid in HTTP/3 ----
TEST_F(MessageComplianceTest, Status101RejectedInHttp3) {
    HttpFields headers{{":status", "101"}};
    auto wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(wire, nullptr);
    mock_stream_1_->SimulateRead(wire, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(client_connection_->error_code_, kMessageError);
}

// ---- H6g: body cap is per server instance ----
TEST_F(MessageComplianceTest, PerInstanceRequestBodyCap) {
    // Dedicated server with a tiny cap on the same client stream pair.
    auto tiny_server = std::make_shared<MockServer>(qpack_encoder_, qpack_decoder_, mock_stream_2_, /*body_cap=*/10);

    HttpFields headers{
        {":method", "POST"},
        {":scheme", "https"},
        {":path", "/upload"},
        {":authority", "example.com"},
        {"content-length", "11"},  // declared body exceeds the tiny cap
    };
    auto wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(wire, nullptr);
    mock_stream_2_->SimulateRead(wire, /*is_last=*/false, /*error=*/0);

    EXPECT_NE(tiny_server->error_code_, 0u) << "oversized declaration must be rejected under the tiny cap";

    // The default-cap server (16 MiB) accepts the same declaration: proves
    // the limit is read from THIS server instance, not a shared static.
    EXPECT_EQ(server_connection_->error_code_, 0u);
}

// ==================== H6f: push id validation ====================

TEST(PushReceiverStreamTest, RejectedPushIdErrorsStream) {
    auto enc = std::make_shared<QpackDecoder>();
    auto stream = std::make_shared<quic::MockQuicStream>(16, StreamDirection::kRecv);

    uint32_t error_code = 0;
    auto push_stream = std::make_shared<PushReceiverStream>(enc, stream,
        [&error_code](uint64_t, uint32_t err) { error_code = err; },
        [](std::shared_ptr<IResponse>, uint32_t) {},
        /*push_id_validator=*/[](uint64_t) { return false; });  // always reject

    // Push stream wire: varint push id 3 (single byte).
    auto wire = FreshBuffer(16);
    uint8_t pid = 0x03;
    wire->Write(&pid, 1);
    stream->SimulateRead(wire, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(error_code, kIdError) << "validator rejection must surface as H3_ID_ERROR";
}

TEST(PushReceiverStreamTest, AcceptedPushIdProceeds) {
    auto enc = std::make_shared<QpackDecoder>();
    auto stream = std::make_shared<quic::MockQuicStream>(17, StreamDirection::kRecv);

    uint32_t error_code = 0;
    auto push_stream = std::make_shared<PushReceiverStream>(enc, stream,
        [&error_code](uint64_t, uint32_t err) { error_code = err; },
        [](std::shared_ptr<IResponse>, uint32_t) {},
        /*push_id_validator=*/[](uint64_t) { return true; });

    auto wire = FreshBuffer(16);
    uint8_t pid = 0x03;
    wire->Write(&pid, 1);
    stream->SimulateRead(wire, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(error_code, 0u) << "accepted push id must not error the stream";
}

// ==================== H5a: instruction streams are BYTE streams ====================

TEST(QpackInstructionStreamTest, EncoderInstructionSplitAcrossFrames) {
    // A dynamic-table insert instruction whose wire form is split across two
    // OnData deliveries must be reassembled at the instruction boundary.
    auto decoder_side = std::make_shared<QpackDecoder>();  // decoder's table state
    // The decoder's dynamic table must have capacity for the incoming
    // insert, or AddHeaderItem rejects it as an oversized entry (a valid
    // insert larger than the table is a protocol error, not a wait state).
    decoder_side->SetMaxTableCapacity(4096);
    auto registry = std::make_shared<QpackBlockedRegistry>();
    auto stream = std::make_shared<quic::MockQuicStream>(18, StreamDirection::kRecv);

    uint32_t error_code = 0;
    auto recv_stream = std::make_shared<QpackEncoderReceiverStream>(stream, decoder_side, registry,
        [&error_code](uint64_t, uint32_t err) { error_code = err; });

    // Generate a real Insert With Literal Name instruction on the wire.
    QpackEncoder emitter;
    std::vector<std::pair<std::string, std::string>> inserts{{"x-split-name", "x-split-value"}};
    auto instr = FreshBuffer(256);
    ASSERT_TRUE(emitter.EncodeEncoderInstructions(inserts, instr));
    ASSERT_GT(instr->GetDataLength(), 2u) << "expected a multi-byte insert instruction";

    // Split anywhere in the middle (not at 0, not at the end).
    uint32_t total = instr->GetDataLength();
    uint32_t split = total / 2;
    ASSERT_GT(split, 0u);
    ASSERT_LT(split, total);

    uint8_t raw[256];
    ASSERT_EQ(instr->ReadNotMovePt(raw, total), total);

    // Model the REAL RecvStream semantics: the read callback receives the
    // stream's persistent buffer, and unconsumed bytes stay in it across
    // deliveries. So both halves go into ONE buffer: after the first
    // delivery only the first half is written; the second delivery appends
    // the rest and re-delivers the same (still-unconsumed-prefix) buffer.
    auto stream_buf = FreshBuffer(256);

    stream_buf->Write(raw, split);
    stream->SimulateRead(stream_buf, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(error_code, 0u) << "a split instruction tail must wait, not error";
    EXPECT_EQ(decoder_side->GetInsertCount(), 0u) << "half an instruction must not mutate the table";

    stream_buf->Write(raw + split, total - split);
    stream->SimulateRead(stream_buf, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(error_code, 0u);
    EXPECT_EQ(decoder_side->GetInsertCount(), 1u) << "the reassembled instruction must take effect exactly once";
}

TEST(QpackInstructionStreamTest, DecoderInstructionSplitAcrossFrames) {
    // An Insert Count Increment instruction split across two OnData
    // deliveries must reassemble before reaching the encoder's KRC.
    auto local_encoder = std::make_shared<QpackEncoder>();
    auto registry = std::make_shared<QpackBlockedRegistry>();
    auto stream = std::make_shared<quic::MockQuicStream>(19, StreamDirection::kRecv);

    uint32_t error_code = 0;
    auto recv_stream = std::make_shared<QpackDecoderReceiverStream>(stream, local_encoder, registry,
        [&error_code](uint64_t, uint32_t err) { error_code = err; });

    // ICI with a saturated 6-bit prefix: first byte 0x3F (increment 63 ==
    // max-in-prefix, needs continuation), then one continuation byte +3 →
    // decoded delta = 63 + 3 = 66.
    QpackInsertCountIncrementFrame frame;
    frame.Set(66);
    auto instr = FreshBuffer(64);
    ASSERT_TRUE(frame.Encode(instr));
    ASSERT_EQ(instr->GetDataLength(), 2u) << "delta 66 must encode as 0x3F 0x03";

    // Real RecvStream semantics: one persistent buffer, unconsumed tail
    // stays across deliveries (see EncoderInstructionSplitAcrossFrames).
    auto stream_buf = FreshBuffer(16);

    uint8_t first = 0x3F;
    stream_buf->Write(&first, 1);
    stream->SimulateRead(stream_buf, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(error_code, 0u) << "split ICI must wait";
    EXPECT_EQ(local_encoder->GetKnownReceivedCount(), 0u);

    uint8_t cont = 0x03;
    stream_buf->Write(&cont, 1);
    stream->SimulateRead(stream_buf, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(error_code, 0u);
    EXPECT_EQ(local_encoder->GetKnownReceivedCount(), 66u) << "reassembled ICI must advance KRC by 66";
}

// ---- H6c regression guard: duplicate content-length with IDENTICAL values
// is legal (RFC 9110 §8.6) and must not be rejected ----
TEST_F(MessageComplianceTest, IdenticalDuplicateContentLengthAccepted) {
    server_connection_->user_handler_ = [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
        resp->SetStatusCode(200);
        resp->AppendBody("ok");
    };

    HttpFields headers{
        {":method", "POST"},
        {":scheme", "https"},
        {":path", "/upload"},
        {":authority", "example.com"},
        {"content-length", "5"},
        {"content-length", "5"},
    };
    auto wire = BuildHeadersFrame(*qpack_encoder_, headers);
    ASSERT_NE(wire, nullptr);
    auto data_wire = BuildDataFrame("12345");
    ASSERT_NE(data_wire, nullptr);

    FeedServer(Concat(wire, data_wire), /*is_last=*/false);

    EXPECT_EQ(server_connection_->error_code_, 0u) << "identical duplicates are legal";
    EXPECT_EQ(server_connection_->handler_invocations_, 1);
}

// ---- H6d: DATA after the trailers section is malformed (RFC 9114 §4.1) ----
TEST_F(MessageComplianceTest, DataAfterTrailersRejected) {
    server_connection_->user_handler_ = [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse> resp) {
        resp->SetStatusCode(200);
    };

    HttpFields headers{
        {":method", "POST"},
        {":scheme", "https"},
        {":path", "/upload"},
        {":authority", "example.com"},
        {"content-length", "5"},
    };
    auto headers_wire = BuildHeadersFrame(*qpack_encoder_, headers);
    auto data_wire = BuildDataFrame("12345");
    ASSERT_NE(headers_wire, nullptr);
    ASSERT_NE(data_wire, nullptr);
    FeedServer(Concat(headers_wire, data_wire), /*is_last=*/false);

    HttpFields trailers{{"x-checksum", "aa"}};
    auto trailers_wire = BuildHeadersFrame(*qpack_encoder_, trailers);
    ASSERT_NE(trailers_wire, nullptr);
    FeedServer(trailers_wire, /*is_last=*/false);
    ASSERT_EQ(server_connection_->error_code_, 0u);

    // A DATA frame AFTER the trailers section: the trailing field section
    // is the final frame of the message (RFC 9114 §4.1).
    auto stray_data = BuildDataFrame("stray");
    ASSERT_NE(stray_data, nullptr);
    FeedServer(stray_data, /*is_last=*/false);

    EXPECT_EQ(server_connection_->error_code_, kMessageError);
    EXPECT_EQ(server_connection_->handler_invocations_, 1) << "no re-routing, no second handler run";
}

// ---- H6e: multiple consecutive informational responses before the final ----
TEST_F(MessageComplianceTest, MultipleInformationalThenFinal) {
    HttpFields info1{{":status", "100"}};
    auto w1 = BuildHeadersFrame(*qpack_encoder_, info1);
    ASSERT_NE(w1, nullptr);
    mock_stream_1_->SimulateRead(w1, /*is_last=*/false, /*error=*/0);

    HttpFields info2{{":status", "103"}, {"link", "</style.css>; rel=preload"}};
    auto w2 = BuildHeadersFrame(*qpack_encoder_, info2);
    ASSERT_NE(w2, nullptr);
    mock_stream_1_->SimulateRead(w2, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(client_connection_->response_invocations_, 0) << "no 1xx may complete the request";
    EXPECT_EQ(client_connection_->error_code_, 0u);

    // Final response with an explicit empty body (content-length: 0)
    // completes the request without needing FIN.
    HttpFields final_resp{{":status", "204"}, {"content-length", "0"}};
    auto w3 = BuildHeadersFrame(*qpack_encoder_, final_resp);
    ASSERT_NE(w3, nullptr);
    mock_stream_1_->SimulateRead(w3, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(client_connection_->response_invocations_, 1);
    ASSERT_NE(client_connection_->response_, nullptr);
    EXPECT_EQ(client_connection_->response_->GetStatusCode(), 204);
}

// ---- H6e: response body cap aborts with EXCESSIVE_LOAD ----
TEST_F(MessageComplianceTest, ResponseBodyCapExceeded) {
    // 8-byte cap; declare a 4-byte body, send 4 (fine), then a second DATA
    // chunk pushing past the cap.
    client_connection_->request_stream_->SetMaxResponseBodySize(8);

    HttpFields resp{{":status", "200"}, {"content-length", "16"}};
    auto headers_wire = BuildHeadersFrame(*qpack_encoder_, resp);
    ASSERT_NE(headers_wire, nullptr);
    mock_stream_1_->SimulateRead(headers_wire, /*is_last=*/false, /*error=*/0);
    ASSERT_EQ(client_connection_->error_code_, 0u);

    auto data_wire = BuildDataFrame(std::string(9, 'x'));
    ASSERT_NE(data_wire, nullptr);
    mock_stream_1_->SimulateRead(data_wire, /*is_last=*/false, /*error=*/0);

    EXPECT_EQ(client_connection_->error_code_, kExcessiveLoad);
}

// ---- H5a: an instruction tail that grows past the 64 KiB cap without ever
// completing is a protocol error, not an unbounded wait ----
TEST(QpackInstructionStreamTest, OversizedTailErrorsStream) {
    auto local_encoder = std::make_shared<QpackDecoder>();
    auto registry = std::make_shared<QpackBlockedRegistry>();
    auto stream = std::make_shared<quic::MockQuicStream>(20, StreamDirection::kRecv);

    uint32_t error_code = 0;
    auto recv_stream = std::make_shared<QpackEncoderReceiverStream>(stream, local_encoder, registry,
        [&error_code](uint64_t, uint32_t err) { error_code = err; });

    // Real RecvStream semantics: one persistent buffer whose unconsumed
    // tail accumulates. Feed 64 KiB + 1 continuation bytes (0xFF): the
    // varint never terminates, the tail crosses the 64 KiB pending cap
    // (kMaxPendingInstrBytes), and the stream must error instead of
    // buffering forever.
    constexpr uint32_t kCap = 64 * 1024;  // mirrors kMaxPendingInstrBytes
    auto stream_buf = FreshBuffer(kCap + 64);
    std::vector<uint8_t> filler(kCap + 1, 0xFF);
    stream_buf->Write(filler.data(), static_cast<uint32_t>(filler.size()));
    stream->SimulateRead(stream_buf, /*is_last=*/false, /*error=*/0);

    EXPECT_NE(error_code, 0u) << "a >64KiB unterminated tail must error the stream";
}

// ---- H5e: a field block referencing a dynamic entry at or above its own
// Required Insert Count is malformed (RFC 9204 §2.1.2) ----
TEST(QpackRicBoundaryTest, DynamicRefAtOrAboveRicRejected) {
    // Build a decoder-side table with 5 real inserts.
    auto decoder_side = std::make_shared<QpackDecoder>();
    decoder_side->SetMaxTableCapacity(4096);

    QpackEncoder emitter;
    std::vector<std::pair<std::string, std::string>> inserts;
    inserts.reserve(5);
    for (int i = 0; i < 5; ++i) {
        inserts.push_back({"x-ric-" + std::to_string(i), "v" + std::to_string(i)});
    }
    auto ins = FreshBuffer(512);
    ASSERT_TRUE(emitter.EncodeEncoderInstructions(inserts, ins));
    ASSERT_TRUE(decoder_side->DecodeEncoderInstructions(ins));
    ASSERT_EQ(decoder_side->GetInsertCount(), 5u);

    // Hand-crafted malicious block: header (RIC=3, base=0, S=0 → single
    // byte 0x03), then an Indexed Field With Dynamic reference (pattern
    // 11, 6-bit delta=1 → 0xC1): abs_index = insert_count(5) - 1 = 4 ≥ 3.
    // The entry EXISTS in the table (so the pre-fix code returned its
    // headers happily), but §2.1.2 makes RIC an exclusive upper bound —
    // the block was encoded against a table state that did not include it.
    auto block = FreshBuffer(64);
    uint8_t wire[] = {0x03, 0xC1};
    block->Write(wire, sizeof(wire));

    HttpFields out;
    EXPECT_FALSE(decoder_side->Decode(block, out))
        << "dynamic reference at abs_index >= RIC must be rejected";
}

}  // namespace
}  // namespace http3
}  // namespace quicx
