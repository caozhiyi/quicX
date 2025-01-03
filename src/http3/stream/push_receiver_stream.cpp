#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/http/response.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/headers_frame.h"
#include "common/buffer/buffer_read_view.h"
#include "http3/stream/push_receiver_stream.h"

namespace quicx {
namespace http3 {

PushReceiverStream::PushReceiverStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const http_response_handler& response_handler):
    IStream(error_handler),
    qpack_encoder_(qpack_encoder),
    stream_(stream),
    response_handler_(response_handler) {

    stream_->SetStreamReadCallBack(std::bind(&PushReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
    // TODO send callback
}

PushReceiverStream::~PushReceiverStream() {
    if (stream_) {
        stream_->Close();
    }
}

 void PushReceiverStream::OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("IStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    std::vector<std::shared_ptr<IFrame>> frames;
    if (!DecodeFrames(data, frames)) {
        common::LOG_ERROR("IStream::OnData decode frames error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_MESSAGE_ERROR);
        return;
    }

    for (const auto& frame : frames) {
        switch (frame->GetType()) {
            case FT_HEADERS:
                HandleHeaders(frame);
                break;
            case FT_DATA:
                HandleData(frame);
                break;
            default:
                common::LOG_ERROR("PushReceiverStream::OnData unknown frame type: %d", frame->GetType());
                error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_FRAME_UNEXPECTED);
                break;
        }
    }
 }

 void PushReceiverStream::HandleHeaders(std::shared_ptr<IFrame> frame) {
    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    if (!headers_frame) {   
        common::LOG_ERROR("IStream::HandleHeaders error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_FRAME_UNEXPECTED);
        return;
    }

    // TODO check if headers is complete and headers length is correct

    // Decode headers using QPACK
    std::vector<uint8_t> encoded_fields = headers_frame->GetEncodedFields();
    auto headers_buffer = (std::make_shared<common::BufferReadView>(encoded_fields.data(), encoded_fields.size()));
    if (!qpack_encoder_->Decode(headers_buffer, headers_)) {
        common::LOG_ERROR("IStream::HandleHeaders error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_MESSAGE_ERROR);
        return;
    }

    if (headers_.find("content-length") != headers_.end()) {
        body_length_ = std::stoul(headers_["content-length"]);

    } else {
        body_length_ = 0;
    }
    
    if (body_length_ == 0) {
        HandleBody();
    }
 }

 void PushReceiverStream::HandleData(std::shared_ptr<IFrame> frame) {
    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    if (!data_frame) {
        common::LOG_ERROR("IStream::HandleData error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_MESSAGE_ERROR);
        return;
    }

    const auto& data = data_frame->GetData();
    body_.insert(body_.end(), data.begin(), data.end());

    if (body_length_ == body_.size()) {
        HandleBody();
    }
 }

 void PushReceiverStream::HandleBody() {
    std::shared_ptr<IResponse> response = std::make_shared<Response>();
    response->SetHeaders(headers_);
    response->SetBody(std::string(body_.begin(), body_.end())); // TODO: do not copy body
    response_handler_(response, 0);
 }

}
}
