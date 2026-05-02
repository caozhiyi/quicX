
#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

#include "http3/frame/push_promise_frame.h"
#include "http3/http/error.h"
#include "http3/http/response.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/request_stream.h"

namespace quicx {
namespace http3 {

// Constructor for complete mode
RequestStream::RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream, std::shared_ptr<IAsyncClientHandler> async_handler,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>& push_promise_handler):
    ReqRespBaseStream(qpack_encoder, blocked_registry, stream, error_handler),
    async_handler_(async_handler),
    body_length_(0),
    received_body_length_(0),
    push_promise_handler_(push_promise_handler) {}

RequestStream::RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream, http_response_handler response_handler,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>& push_promise_handler):
    ReqRespBaseStream(qpack_encoder, blocked_registry, stream, error_handler),
    response_handler_(response_handler),
    body_length_(0),
    received_body_length_(0),
    push_promise_handler_(push_promise_handler) {}

RequestStream::~RequestStream() {
    // Do NOT call Close() here - it should have been called by the base class after sending request
}

bool RequestStream::SendRequest(std::shared_ptr<IRequest> request) {
    PseudoHeader::Instance().EncodeRequest(request);

    // Check if using streaming mode for request body sending
    auto body_provider = request->GetRequestBodyProvider();

    auto body_buffer = request->GetBody();
    if (!body_provider && body_buffer && body_buffer->GetDataLength() > 0) {
        common::LOG_DEBUG("SendRequest: adding content-length: %zu", body_buffer->GetDataLength());
        request->AddHeader("content-length", std::to_string(body_buffer->GetDataLength()));
    }

    // send headers
    if (!SendHeaders(request->GetHeaders())) {
        common::LOG_ERROR("SendHeaders error");
        return false;
    }

    // if body provider is set, send body using provider (streaming mode)
    if (body_provider) {
        return SendBodyWithProvider(body_provider);

        // if body is set, send body directly
    } else if (body_buffer && body_buffer->GetDataLength() > 0) {
        return SendBodyDirectly(std::dynamic_pointer_cast<common::IBuffer>(body_buffer));
    }

    // No body to send (e.g. GET request): we must close the sending direction
    // so the peer sees FIN and treats the request as complete. Without this
    // the server (e.g. ngtcp2) will wait forever for more data on the
    // request stream and never produce a response.
    if (stream_) {
        stream_->Close();
        common::LOG_DEBUG("SendRequest: no body, sent FIN on request stream id=%llu",
            static_cast<unsigned long long>(GetStreamID()));
    }
    return true;
}

void RequestStream::HandleHeaders() {
    // parse header
    response_ = std::make_shared<Response>();
    response_->SetHeaders(headers_);

    PseudoHeader::Instance().DecodeResponse(response_);

    common::LOG_DEBUG("RequestStream::HandleHeaders: headers decoded");
    // Metrics: Track HTTP/3 response status codes
    int status_code = response_->GetStatusCode();
    if (status_code >= 200 && status_code < 300) {
        common::Metrics::CounterInc(common::MetricsStd::Http3Responses2xx);
    } else if (status_code >= 300 && status_code < 400) {
        common::Metrics::CounterInc(common::MetricsStd::Http3Responses3xx);
    } else if (status_code >= 400 && status_code < 500) {
        common::Metrics::CounterInc(common::MetricsStd::Http3Responses4xx);
    } else if (status_code >= 500 && status_code < 600) {
        common::Metrics::CounterInc(common::MetricsStd::Http3Responses5xx);
    }

    bool has_content_length = false;
    if (headers_.find("content-length") != headers_.end()) {
        try {
            body_length_ = std::stoul(headers_["content-length"]);
            has_content_length = true;
            common::LOG_DEBUG("RequestStream::HandleHeaders: content-length found: %u", body_length_);
        } catch (const std::exception& e) {
            common::LOG_ERROR("RequestStream::HandleHeaders: invalid content-length value '%s': %s",
                headers_["content-length"].c_str(), e.what());
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
            return;
        }
    } else {
        common::LOG_DEBUG("RequestStream::HandleHeaders: content-length NOT found");
    }

    if (async_handler_) {
        async_handler_->OnHeaders(response_);

        // For responses with no body, signal completion immediately
        // OnBodyChunk will be called with (nullptr, 0, true) when FIN arrives
        // but if there's no body at all, we rely on the FIN handler in HandleData

    } else if (response_handler_) {
        common::LOG_DEBUG("RequestStream::HandleHeaders: checking completion condition. has_cl=%d, body_len=%u",
            has_content_length, body_length_);
        // Complete mode: only call handler if no body expected
        if (!has_content_length || body_length_ == 0) {
            common::LOG_DEBUG("RequestStream::HandleHeaders: calling response handler (no body expected)");
            response_handler_(response_, 0);

            // CRITICAL: Notify connection that stream is complete and can be removed
            // For responses with no body, this is the only place to signal completion
            error_handler_(GetStreamID(), 0);
        }
        // If body_length_ > 0, wait for HandleData to receive and process the body
    }
}

void RequestStream::HandleData(const std::shared_ptr<common::IBuffer>& data, bool is_last) {
    // Validate data size
    if (body_length_ > 0 && received_body_length_ + data->GetDataLength() > body_length_) {
        common::LOG_ERROR("ReqRespBaseStream::HandleData: received more data than content-length, expected %u, got %zu",
            body_length_, received_body_length_ + data->GetDataLength());
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }

    uint32_t data_length = data->GetDataLength();
    received_body_length_ += data_length;

    // Metrics: Track response bytes received (client side)
    if (data_length > 0) {
        common::Metrics::CounterInc(common::MetricsStd::Http3ResponseBytesRx, data_length);
    }

    // Streaming mode: call handler immediately
    if (async_handler_) {
        // Count total chunks first to identify the last one
        size_t total_chunks = data->GetChunkCount();

        if (total_chunks > 0) {
            // Now visit again and mark only the LAST chunk as is_last
            size_t current_chunk = 0;
            data->VisitData([&](uint8_t* chunk_data, uint32_t length) {
                current_chunk++;
                bool is_last_chunk = is_last && (current_chunk == total_chunks);
                async_handler_->OnBodyChunk(chunk_data, length, is_last_chunk);
                return true;
            });

        } else {
            if (is_last) {
                async_handler_->OnBodyChunk(nullptr, 0, is_last);
            }
        }

        // CRITICAL: Consume the data from the buffer after processing
        // This frees up space in RecvStream's buffer for more incoming data
        if (data_length > 0) {
            data->MoveReadPt(data_length);
        }

        // CRITICAL: Defer stream completion notification to avoid double-call
        if (is_last || is_last_data_) {
            should_notify_completion_ = true;
        }
        return;
    }

    // Complete mode: accumulate to body_
    common::LOG_DEBUG("RequestStream::HandleData: accumulating body. received=%u, expected=%u, is_last=%d",
        received_body_length_, body_length_, is_last);

    if (data_length > 0) {
        data->VisitData([&](uint8_t* ptr, uint32_t len) {
            response_->AppendBody(ptr, len);
            return true;
        });
        data->MoveReadPt(data_length);
    }

    // Only call handler when all data is received
    if (body_length_ == received_body_length_ || is_last) {
        common::LOG_DEBUG("RequestStream::HandleData: calling response handler (complete)");
        response_handler_(response_, 0);

        // CRITICAL: Defer stream completion notification until after all frames in current batch
        // are processed. This ensures PUSH_PROMISE frames (which come after DATA) are not ignored.
        // The parent class will call error_handler_ after processing all frames in OnData().
        // We need to delay notification when:
        // 1. We received all expected data (body_length_ == received_body_length_), OR
        // 2. We received FIN (is_last), OR
        // 3. The stream received FIN in the batch (is_last_data_)
        // This handles the case where DATA frame is not the last frame in the batch.
        if (is_last || body_length_ == received_body_length_ || is_last_data_) {
            should_notify_completion_ = true;
        }
    }
}

void RequestStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    common::LOG_DEBUG("RequestStream::HandleFrame: processing frame type=0x%x", 
        static_cast<uint32_t>(frame->GetType()));
    switch (frame->GetType()) {
        case FrameType::kPushPromise:
            common::LOG_DEBUG("RequestStream::HandleFrame: dispatching to HandlePushPromise");
            HandlePushPromise(frame);
            break;
        default:
            ReqRespBaseStream::HandleFrame(frame);
            break;
    }
}

void RequestStream::HandlePushPromise(std::shared_ptr<IFrame> frame) {
    auto push_promise_frame = std::static_pointer_cast<PushPromiseFrame>(frame);
    // Decode headers using QPACK
    std::unordered_map<std::string, std::string> headers;
    auto encoded_fields = push_promise_frame->GetEncodedFields();
    if (!qpack_encoder_->Decode(encoded_fields, headers)) {
        common::LOG_ERROR("RequestStream::HandlePushPromise error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return;
    }

    push_promise_handler_(headers, push_promise_frame->GetPushId());
}

void RequestStream::HandleFinWithoutData() {
    // Called when FIN is received but no HTTP/3 frames were decoded
    // This can happen with small responses or when frames are incomplete
    if (async_handler_) {
        // Notify the async handler that the stream has ended
        async_handler_->OnBodyChunk(nullptr, 0, true);
        // Defer stream completion notification to avoid double-call to error_handler_
        should_notify_completion_ = true;
    } else if (response_handler_) {
        // For complete mode, call the response handler
        response_handler_(response_, 0);
        // Defer stream completion notification to avoid double-call to error_handler_
        should_notify_completion_ = true;
    }
}

}  // namespace http3
}  // namespace quicx