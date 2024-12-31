#include "common/log/log.h"
#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/request_stream.h"
#include "http3/frame/push_promise_frame.h"


namespace quicx {
namespace http3 {

RequestStream::RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
    const std::function<void(uint64_t id, int32_t error)>& error_handler,
    const http_response_handler& response_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&)>& push_promise_handler):
    ReqRespBaseStream(qpack_encoder, stream, error_handler),
    response_handler_(response_handler),
    push_promise_handler_(push_promise_handler) {

}

RequestStream::~RequestStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool RequestStream::SendRequest(const IRequest& request) {
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(request.GetHeaders(), headers_buffer)) {
        return false;
    }
    
    // Send HEADERS frame
    HeadersFrame headers_frame;
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    headers_frame.SetEncodedFields(encoded_fields);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!headers_frame.Encode(frame_buffer)) {
        return false;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        return false;
    }

    // Send DATA frame if body exists
    if (request.GetBody().length() > 0) {
        DataFrame data_frame; // TODO: may send more than one DATA frame
        std::vector<uint8_t> body(request.GetBody().begin(), request.GetBody().end());
        data_frame.SetData(body);           

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            return false;
        }
        if (stream_->Send(data_buffer) <= 0) {
            return false;
        }
    }

    return true;
}

void RequestStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (frame->GetType()) {
        case FT_PUSH_PROMISE:
            HandlePushPromise(frame);
            break;
        default:
            ReqRespBaseStream::HandleFrame(frame);
            break;
    }
}

void RequestStream::HandleBody() {
    Response response;
    response.SetHeaders(headers_);
    response.SetBody(std::string(body_.begin(), body_.end())); // TODO: do not copy body
    response_handler_(response, 0);
}

void RequestStream::HandlePushPromise(std::shared_ptr<IFrame> frame) {
    auto push_promise_frame = std::static_pointer_cast<PushPromiseFrame>(frame);
    // Decode headers using QPACK
    std::unordered_map<std::string, std::string> headers;
    std::vector<uint8_t> encoded_fields = push_promise_frame->GetEncodedFields();
    std::shared_ptr<common::IBufferRead> headers_buffer = std::make_shared<common::Buffer>(encoded_fields.data(), encoded_fields.size());
    if (!qpack_encoder_->Decode(headers_buffer, headers)) {
        common::LOG_ERROR("RequestStream::HandlePushPromise error");
        return;
    }

    push_promise_handler_(headers);
}

}
}