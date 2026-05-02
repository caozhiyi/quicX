#include "common/buffer/multi_block_buffer.h"
#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

#include "quic/quicx/global_resource.h"

#include "http3/frame/push_promise_frame.h"
#include "http3/http/error.h"
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/response_stream.h"

namespace quicx {
namespace http3 {

ResponseStream::ResponseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream, std::shared_ptr<IHttpProcessor> http_processor,
    const std::function<void(std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>)> push_handler,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<bool()>& settings_received_cb):
    ReqRespBaseStream(qpack_encoder, blocked_registry, stream, error_handler),
    body_length_(0),
    is_response_sent_(false),
    received_body_length_(0),
    http_processor_(http_processor),
    push_handler_(push_handler),
    settings_received_cb_(settings_received_cb) {}

ResponseStream::~ResponseStream() {
    // Do NOT call Close() here - it should have been called by the base class after sending response
}

void ResponseStream::SendPushPromise(const std::unordered_map<std::string, std::string>& headers, int32_t push_id) {
    // Create and encode push promise frame
    PushPromiseFrame push_frame;
    push_frame.SetPushId(push_id);

    auto headers_buffer =
        std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        common::LOG_ERROR("ResponseStream::SendPushPromise qpack encode error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return;
    }

    // Set encoded fields in push promise frame
    push_frame.SetEncodedFields(headers_buffer);

    auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!push_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("ResponseStream::SendPushPromise frame encode error");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        return;
    }

    // Send frame to client
    stream_->Flush();
}

bool ResponseStream::SendResponse(std::shared_ptr<IResponse> response) {
    auto body = response->GetBody();
    size_t body_size = body ? body->GetDataLength() : 0;
    common::LOG_DEBUG("ResponseStream::SendResponse: status=%d, body size=%zu", response->GetStatusCode(), body_size);

    // Encode pseudo-headers (including :status)
    PseudoHeader::Instance().EncodeResponse(response);

    // RFC 9110 Section 9.3.2: HEAD responses MUST NOT contain a body
    // But MAY include Content-Length header (representing what GET would return)
    bool is_head_request = (request_ && request_->GetMethod() == HttpMethod::kHead);

    // Add content-length if body exists (even for HEAD, to indicate GET body size)
    if (body && body_size > 0) {
        response->AddHeader("content-length", std::to_string(body_size));
    }

    // Send headers
    if (!SendHeaders(response->GetHeaders())) {
        common::LOG_ERROR("ResponseStream::SendResponse: SendHeaders failed");
        return false;
    }
    common::LOG_DEBUG("ResponseStream::SendResponse: headers sent successfully");

    // For HEAD requests, never send body regardless of response content
    if (is_head_request) {
        stream_->Close();
        common::LOG_DEBUG("ResponseStream::SendResponse: HEAD request, no body sent, stream closed with FIN");
        // Signal stream completion to the connection
        if (error_handler_) {
            error_handler_(GetStreamID(), 0);
        }
        return true;
    }

    // Send body (for non-HEAD requests)
    auto provider = response->GetResponseBodyProvider();
    if (provider) {
        // Streaming mode: use body provider
        common::LOG_DEBUG("ResponseStream::SendResponse: using body provider");
        return SendBodyWithProvider(provider);

    } else if (body && body_size > 0) {
        // Complete mode: send entire body
        common::LOG_DEBUG("ResponseStream::SendResponse: sending body directly, size=%zu", body_size);
        bool result = SendBodyDirectly(std::dynamic_pointer_cast<common::IBuffer>(body));

        // Metrics: Track response bytes sent (server side)
        if (result) {
            common::Metrics::CounterInc(common::MetricsStd::Http3ResponseBytesTx, body_size);
        }

        return result;
    }

    // No body to send - close stream with FIN and signal completion
    stream_->Close();
    common::LOG_DEBUG("ResponseStream::SendResponse: no body to send, stream closed with FIN");

    // Signal stream completion to the connection
    if (error_handler_) {
        error_handler_(GetStreamID(), 0);
    }
    return true;
}

void ResponseStream::HandleHeaders() {
    // Note: We do NOT reject requests when peer SETTINGS hasn't been received yet.
    // In HTTP/3, the control stream (carrying SETTINGS) and request streams are
    // independent QUIC streams. Due to QUIC's stream multiplexing, a request may
    // arrive before the control stream's SETTINGS frame is processed. This is a
    // normal race condition, not a protocol violation. The H3_MISSING_SETTINGS
    // error (0x010a) should only be used when the control stream's first frame
    // is not SETTINGS, not when requests arrive before SETTINGS on another stream.
    if (settings_received_cb_ && !settings_received_cb_()) {
        common::LOG_WARN("ResponseStream: Request received before peer SETTINGS frame, proceeding with defaults");
    }

    // Create request and response objects FIRST (before calling base class)
    // This ensures they are available if HandleBody() is called from base class
    request_ = std::make_shared<Request>();
    response_ = std::make_shared<Response>();

    request_->SetHeaders(headers_);

    PseudoHeader::Instance().DecodeRequest(request_);

    bool has_content_length = false;
    if (headers_.find("content-length") != headers_.end()) {
        try {
            body_length_ = std::stoul(headers_["content-length"]);
            has_content_length = true;
        } catch (const std::exception& e) {
            common::LOG_ERROR("ResponseStream::HandleHeaders: invalid content-length value '%s': %s",
                headers_["content-length"].c_str(), e.what());
            if (error_handler_) {
                error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
            }
            return;
        }
    }

    route_config_ = http_processor_->MatchRoute(request_->GetMethod(), request_->GetPath(), request_);

    // if async server handler is set, call the handler
    if (route_config_.IsAsyncServer()) {
        HandleHttp(
            std::bind(&IAsyncServerHandler::OnHeaders, route_config_.GetAsyncServerHandler(), request_, response_));
        common::LOG_DEBUG("HandleHeaders: async server handler called");
        return;
    }

    common::LOG_DEBUG("HandleHeaders: complete handler called, body_length_: %u, has_content_length: %d", body_length_,
        has_content_length);
    // if complete handler is set and there is no body, call the handler and send response
    if (!has_content_length || body_length_ == 0) {
        // No body expected, handle immediately
        HandleHttp(route_config_.GetCompleteHandler());
        HandleResponse();
        common::LOG_DEBUG("HandleHeaders: complete handler called and response sent");

        // NOTE: Do NOT call error_handler_ here to signal stream completion!
        // For body provider mode (e.g., large file downloads), the stream must remain
        // alive until all data is sent. The stream completion will be signaled in
        // HandleSent() when all_provider_data_sent_ becomes true, OR for direct body
        // mode, in SendBodyDirectly() after the FIN is sent.
        //
        // Previously, calling error_handler_(GetStreamID(), 0) here caused the
        // ResponseStream to be removed from the connection's streams_ map, which
        // caused weak_self.lock() to return nullptr in the sended_cb_ callback,
        // preventing HandleSent from being called and body provider from continuing.
        return;
    }
    common::LOG_DEBUG("HandleHeaders: complete handler called");
    // If body_length_ > 0, wait for HandleData to accumulate and process the body
}

void ResponseStream::HandleData(const std::shared_ptr<common::IBuffer>& data, bool is_last) {
    common::LOG_DEBUG(
        "ResponseStream::HandleData: data size=%zu, is_last=%d, body_length_=%u, received_body_length_=%u",
        data->GetDataLength(), is_last, body_length_, received_body_length_);

    // Validate data size
    if (body_length_ > 0 && received_body_length_ + data->GetDataLength() > body_length_) {
        common::LOG_ERROR("ReqRespBaseStream::HandleData: received more data than content-length, expected %u, got %zu",
            body_length_, received_body_length_ + data->GetDataLength());
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        }
        return;
    }

    uint32_t data_length = data->GetDataLength();
    received_body_length_ += data_length;

    // Streaming mode: call handler immediately
    if (route_config_.IsAsyncServer()) {
        auto async_handler = route_config_.GetAsyncServerHandler();

        // Count total chunks first to identify the last one
        size_t total_chunks = data->GetChunkCount();
        if (total_chunks > 0) {
            // Now visit again and mark only the LAST chunk as is_last
            size_t current_chunk = 0;
            data->VisitData([&](uint8_t* chunk_data, uint32_t length) {
                current_chunk++;
                bool is_last_chunk = is_last && (current_chunk == total_chunks);
                async_handler->OnBodyChunk(chunk_data, length, is_last_chunk);
                return true;
            });
        } else {
            if (is_last) {
                async_handler->OnBodyChunk(nullptr, 0, is_last);
            }
        }

        // CRITICAL: Consume the data from the buffer after processing
        // This frees up space in RecvStream's buffer for more incoming data
        if (data_length > 0) {
            data->MoveReadPt(data_length);
        }

        // all data received, send response and handle push
        if (is_last) {
            HandleResponse();

            // Defer stream completion notification to parent class (after all frames processed)
            should_notify_completion_ = true;
        }
        return;
    }

    // Complete mode: accumulate to body_
    if (!body_) {
        body_ = std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    }
    if (data_length > 0) {
        body_->Write(data);
        // CRITICAL: Consume the data from the buffer after copying
        // This frees up space in RecvStream's buffer for more incoming data
        data->MoveReadPt(data_length);
    }

    common::LOG_DEBUG(
        "ResponseStream::HandleData: body_ size now=%zu, checking if complete (body_length_=%u, is_last=%d)",
        body_->GetDataLength(), body_length_, is_last);

    // check if all data received, call the handler and send response
    if ((body_length_ == body_->GetDataLength() || is_last) && !is_response_sent_) {
        is_response_sent_ = true;
        request_->SetBody(body_);
        common::LOG_DEBUG("ResponseStream::HandleData: calling handler and sending response");
        HandleHttp(route_config_.GetCompleteHandler());
        HandleResponse();

        // Defer stream completion notification to parent class (after all frames processed)
        if (is_last || (body_length_ == body_->GetDataLength() && is_last_data_)) {
            should_notify_completion_ = true;
        }
        return;
    }
}

void ResponseStream::HandleHttp(
    std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> handler) {
    // Metrics: HTTP/3 request received
    common::Metrics::CounterInc(common::MetricsStd::Http3RequestsTotal);
    common::Metrics::GaugeInc(common::MetricsStd::Http3RequestsActive);

    http_processor_->BeforeHandlerProcess(request_, response_);
    handler(request_, response_);
    http_processor_->AfterHandlerProcess(request_, response_);

    // Metrics: Request completed
    common::Metrics::GaugeDec(common::MetricsStd::Http3RequestsActive);
}

void ResponseStream::HandleResponse() {
    // send response
    if (!SendResponse(response_)) {
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        }
        common::LOG_ERROR("ResponseStream::HandleHttp send response error");
        return;
    }

    // handle push
    if (push_handler_) {
        push_handler_(response_, std::dynamic_pointer_cast<ResponseStream>(shared_from_this()));
    }
}

void ResponseStream::HandleFinWithoutData() {
    // Called when FIN is received but no HTTP/3 frames were decoded
    // For server side (ResponseStream), this means the client finished sending the request
    if (route_config_.IsAsyncServer()) {
        auto async_handler = route_config_.GetAsyncServerHandler();
        // Notify async handler that request body is complete (empty)
        async_handler->OnBodyChunk(nullptr, 0, true);
        // Send response and complete
        HandleResponse();
        // Defer stream completion notification to avoid double-call to error_handler_
        should_notify_completion_ = true;

    } else {
        // Complete mode: call handler and send response
        if (!is_response_sent_) {
            is_response_sent_ = true;
            HandleHttp(route_config_.GetCompleteHandler());
            HandleResponse();
            // Defer stream completion notification to avoid double-call to error_handler_
            should_notify_completion_ = true;
        }
    }
}

}  // namespace http3
}  // namespace quicx