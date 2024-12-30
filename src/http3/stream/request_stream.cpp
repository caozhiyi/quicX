#include "common/log/log.h"
#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/request_stream.h"


namespace quicx {
namespace http3 {

RequestStream::RequestStream(std::shared_ptr<QpackEncoder> qpack_encoder,
    std::shared_ptr<quic::IQuicBidirectionStream> stream) :
    ReqRespBaseStream(qpack_encoder, stream) {

}

RequestStream::~RequestStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool RequestStream::SendRequest(const IRequest& request, const http_response_handler& handler) {
    handler_ = handler;

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
        case FT_HEADERS:
            HandleHeaders(frame);
            break;
        case FT_DATA:
            HandleData(frame);
            break;
        case FT_PUSH_PROMISE:
            HandlePushPromise(frame);
            break;
        default:
            common::LOG_ERROR("RequestStream::HandleFrame unknown frame type: %d", frame->GetType());
            break;
    }
}

void RequestStream::HandleBody() {
    Response response;
    response.SetHeaders(headers_);
    response.SetBody(std::string(body_.begin(), body_.end())); // TODO: do not copy body
    handler_(response, 0);
}

void RequestStream::HandlePushPromise(std::shared_ptr<IFrame> frame) {
    // TODO: Handle push promise
}

}
}