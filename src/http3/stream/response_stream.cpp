#include "common/buffer/multi_block_buffer.h"
#include "common/log/log.h"

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

    // Add content-length if body exists
    if (body && body_size > 0) {
        response->AddHeader("content-length", std::to_string(body_size));
    }

    // Send headers
    if (!SendHeaders(response->GetHeaders())) {
        common::LOG_ERROR("ResponseStream::SendResponse: SendHeaders failed");
        return false;
    }
    common::LOG_DEBUG("ResponseStream::SendResponse: headers sent successfully");

    // Send body
    auto provider = response->GetResponseBodyProvider();
    if (provider) {
        // Streaming mode: use body provider
        common::LOG_DEBUG("ResponseStream::SendResponse: using body provider");
        return SendBodyWithProvider(provider);

    } else if (body && body_size > 0) {
        // Complete mode: send entire body
        common::LOG_DEBUG("ResponseStream::SendResponse: sending body directly, size=%zu", body_size);
        return SendBodyDirectly(std::dynamic_pointer_cast<common::IBuffer>(body));
    }
    common::LOG_DEBUG("ResponseStream::SendResponse: no body to send, complete");
    return true;
}

void ResponseStream::HandleHeaders() {
    // RFC 9114 Section 4.1: Reject request if SETTINGS not received yet
    if (settings_received_cb_ && !settings_received_cb_()) {
        common::LOG_ERROR("ResponseStream: Request received before SETTINGS frame");
        if (error_handler_) {
            error_handler_(GetStreamID(), Http3ErrorCode::kMissingSettings);
        }
        return;
    }

    // Create request and response objects FIRST (before calling base class)
    // This ensures they are available if HandleBody() is called from base class
    request_ = std::make_shared<Request>();
    response_ = std::make_shared<Response>();

    request_->SetHeaders(headers_);

    PseudoHeader::Instance().DecodeRequest(request_);

    bool has_content_length = false;
    if (headers_.find("content-length") != headers_.end()) {
        has_content_length = true;
        body_length_ = std::stoul(headers_["content-length"]);
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

        // CRITICAL: Notify connection that stream is complete and can be removed
        // For requests with no body, this is the only place to signal completion
        error_handler_(GetStreamID(), 0);
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

            // CRITICAL: Notify connection that stream is complete and can be removed
            // error_code=0 means normal completion (not an actual error)
            error_handler_(GetStreamID(), 0);
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

        // CRITICAL: Notify connection that stream is complete and can be removed
        // error_code=0 means normal completion (not an actual error)
        if (is_last) {
            error_handler_(GetStreamID(), 0);
        }
        return;
    }
}

void ResponseStream::HandleHttp(
    std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> handler) {
    http_processor_->BeforeHandlerProcess(request_, response_);
    handler(request_, response_);
    http_processor_->AfterHandlerProcess(request_, response_);
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
        push_handler_(response_, shared_from_this());
    }
}

}  // namespace http3
}  // namespace quicx