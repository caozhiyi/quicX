#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/http/response.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/request_stream.h"
#include "http3/frame/push_promise_frame.h"

namespace quicx {
namespace http3 {

// Constructor for complete mode
RequestStream::RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream,
    std::shared_ptr<IAsyncClientHandler> async_handler,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>& push_promise_handler):
    ReqRespBaseStream(qpack_encoder, blocked_registry, stream, error_handler),
    async_handler_(async_handler),
    body_length_(0),
    received_body_length_(0),
    push_promise_handler_(push_promise_handler) {

}

RequestStream::RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<QpackBlockedRegistry>& blocked_registry,
    const std::shared_ptr<IQuicBidirectionStream>& stream,
    http_response_handler response_handler,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>& push_promise_handler):
    ReqRespBaseStream(qpack_encoder, blocked_registry, stream, error_handler),
    response_handler_(response_handler),
    body_length_(0),
    received_body_length_(0),
    push_promise_handler_(push_promise_handler) {

}

RequestStream::~RequestStream() {
    // Do NOT call Close() here - it should have been called by the base class after sending request
}

bool RequestStream::SendRequest(std::shared_ptr<IRequest> request) {
    PseudoHeader::Instance().EncodeRequest(request);

    // Check if using streaming mode for request body sending
    auto body_provider = request->GetRequestBodyProvider();
    
    auto body_buffer = request->GetBody();
    if (!body_provider && body_buffer && body_buffer->GetDataLength() > 0) {
        request->AddHeader("content-length", std::to_string(body_buffer->GetDataLength()));
    }

    // send headers
    if (!SendHeaders(request->GetHeaders())) {
        return false;
    }

    // if body provider is set, send body using provider (streaming mode)
    if (body_provider) {
        return SendBodyWithProvider(body_provider);
    
    // if body is set, send body directly
    } else if (body_buffer && body_buffer->GetDataLength() > 0) {
        return SendBodyDirectly(std::dynamic_pointer_cast<common::IBuffer>(body_buffer));
    }
    return true;
}

void RequestStream::HandleHeaders() {
    //parse header 
    response_ = std::make_shared<Response>();
    response_->SetHeaders(headers_);
    
    PseudoHeader::Instance().DecodeResponse(response_);
    
    bool has_content_length = false;
    if (headers_.find("content-length") != headers_.end()) {
        body_length_ = std::stoul(headers_["content-length"]);
        has_content_length = true;
    }
    
    if (async_handler_) {
        async_handler_->OnHeaders(response_);

    } else if (response_handler_) {
        // Complete mode: only call handler if no body expected
        if (!has_content_length || body_length_ == 0) {
            response_handler_(response_, 0);
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

    received_body_length_ += data->GetDataLength();

    // Streaming mode: call handler immediately
    if (async_handler_) {
        data->VisitData([&](uint8_t* data, uint32_t length) {
            async_handler_->OnBodyChunk(data, length, is_last);
        });
        // Streaming mode: call handler immediately chunk as potentially last
        return;
    }
    
    // Complete mode: accumulate to body_    
    if (body_length_ == received_body_length_ || is_last) {
        data->VisitData([&](uint8_t* ptr, uint32_t len) {
            response_->AppendBody(ptr, len);
        });
        response_handler_(response_, 0);
    }
}

void RequestStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FrameType::kPushPromise:
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

}
}