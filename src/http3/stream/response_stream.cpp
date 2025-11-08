#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/response_stream.h"
#include "http3/frame/push_promise_frame.h"

namespace quicx {
namespace http3 {

ResponseStream::ResponseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
    std::shared_ptr<IHttpProcessor> http_processor,
    const std::function<void(std::shared_ptr<IResponse>, std::shared_ptr<ResponseStream>)> push_handler,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    ReqRespBaseStream(qpack_encoder, blocked_registry, stream, error_handler),
    body_length_(0),
    received_body_length_(0),
    http_processor_(http_processor),
    push_handler_(push_handler) {

}

ResponseStream::~ResponseStream() {
    // Do NOT call Close() here - it should have been called by the base class after sending response
}

void ResponseStream::SendPushPromise(const std::unordered_map<std::string, std::string>& headers, int32_t push_id) {
    // Create and encode push promise frame
    PushPromiseFrame push_frame;
    push_frame.SetPushId(push_id);

    // Encode headers using qpack
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        common::LOG_ERROR("ResponseStream::SendPushPromise qpack encode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return;
    }

    // Set encoded fields in push promise frame
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    push_frame.SetEncodedFields(encoded_fields);

    // Encode push promise frame
    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!push_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("ResponseStream::SendPushPromise frame encode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return;
    }

    // Send frame to client
    stream_->Send(frame_buffer);
}

bool ResponseStream::SendResponse(std::shared_ptr<IResponse> response) {
    common::LOG_DEBUG("ResponseStream::SendResponse: status=%d, body size=%zu", 
                     response->GetStatusCode(), response->GetBody().size());
    
    // Encode pseudo-headers (including :status)
    PseudoHeader::Instance().EncodeResponse(response);
    
    // Add content-length if body exists
    if (!response->GetBody().empty()) {
        response->AddHeader("content-length", std::to_string(response->GetBody().size()));
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

    } else if (!response->GetBody().empty()) {
        // Complete mode: send entire body
        common::LOG_DEBUG("ResponseStream::SendResponse: sending body directly, size=%zu", response->GetBody().size());
        return SendBodyDirectly(response->GetBody());
    }
    common::LOG_DEBUG("ResponseStream::SendResponse: no body to send, complete");
    return true;
}

void ResponseStream::HandleHeaders() {
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
    
    route_config_ = http_processor_->MatchRoute(request_->GetMethod(), request_->GetPath());

    // if async server handler is set, call the handler
    if (route_config_.IsAsyncServer()) {
        HandleHttp(std::bind(&IAsyncServerHandler::OnHeaders, route_config_.GetAsyncServerHandler(), request_, response_));
        return;
    }

    // if complete handler is set and there is no body, call the handler and send response
    if (!has_content_length || body_length_ == 0) {
        // No body expected, handle immediately
        HandleHttp(route_config_.GetCompleteHandler());
        HandleResponse();
        return;
    }
    // If body_length_ > 0, wait for HandleData to accumulate and process the body
}

void ResponseStream::HandleData(const std::vector<uint8_t>& data, bool is_last) {
    common::LOG_DEBUG("ResponseStream::HandleData: data size=%zu, is_last=%d, body_length_=%u, received_body_length_=%u", 
                     data.size(), is_last, body_length_, received_body_length_);
    
    // Validate data size
    if (body_length_ > 0 && received_body_length_ + data.size() > body_length_) {
        common::LOG_ERROR("ReqRespBaseStream::HandleData: received more data than content-length, expected %u, got %zu",
                         body_length_, received_body_length_ + data.size());
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }

    received_body_length_ += data.size();

    // Streaming mode: call handler immediately
    if (route_config_.IsAsyncServer()) {
        auto async_handler = route_config_.GetAsyncServerHandler();
        async_handler->OnBodyChunk(data.data(), data.size(), is_last);

        // all data received, send response and handle push
        if (is_last) {
            HandleResponse();
        }
        return;
    }

    // Complete mode: accumulate to body_
    body_.insert(body_.end(), data.begin(), data.end());
    
    common::LOG_DEBUG("ResponseStream::HandleData: body_ size now=%zu, checking if complete (body_length_=%u, is_last=%d)", 
                     body_.size(), body_length_, is_last);
    
    // check if all data received, call the handler and send response
    if (body_length_ == body_.size() || is_last) {
        request_->SetBody(std::string(body_.begin(), body_.end())); // TODO: do not copy body
        common::LOG_DEBUG("ResponseStream::HandleData: calling handler and sending response");
        HandleHttp(route_config_.GetCompleteHandler());
        HandleResponse();
        return;
    }
}

void ResponseStream::HandleHttp(std::function<void(std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response)> handler) {
    http_processor_->BeforeHandlerProcess(request_, response_);
    handler(request_, response_);
    http_processor_->AfterHandlerProcess(request_, response_);
}

void ResponseStream::HandleResponse() {
    // send response
    if (!SendResponse(response_)) {
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        common::LOG_ERROR("ResponseStream::HandleHttp send response error");
        return;
    }

    // handle push
    if (push_handler_) {
        push_handler_(response_, shared_from_this());
    }
}

}
}