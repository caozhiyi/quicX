#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/request_stream.h"
#include "http3/frame/push_promise_frame.h"


namespace quicx {
namespace http3 {

RequestStream::RequestStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const http_response_handler& response_handler,
    const std::function<void(std::unordered_map<std::string, std::string>&, uint64_t push_id)>& push_promise_handler):
    ReqRespBaseStream(qpack_encoder, stream, error_handler),
    response_handler_(response_handler),
    push_promise_handler_(push_promise_handler) {

}

RequestStream::~RequestStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool RequestStream::SendRequest(std::shared_ptr<IRequest> request) {
    PseudoHeader::Instance().EncodeRequest(request);

    if (!request->GetBody().empty()) {
        request->AddHeader("content-length", std::to_string(request->GetBody().size()));
    }

    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(request->GetHeaders(), headers_buffer)) {
        common::LOG_ERROR("RequestStream::SendRequest error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    
    // Send HEADERS frame
    HeadersFrame headers_frame;
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    headers_frame.SetEncodedFields(encoded_fields);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("RequestStream::SendRequest headers frame encode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return false;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("RequestStream::SendRequest send headers error");
        error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
        return false;
    }

    // Send DATA frame if body exists
    if (!request->GetBody().empty()) {
        DataFrame data_frame;
        std::vector<uint8_t> body(request->GetBody().begin(), request->GetBody().begin() + request->GetBody().size());
        data_frame.SetData(body);           

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("RequestStream::SendRequest data frame encode error");
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }
        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("RequestStream::SendRequest send data error");
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
    }

    return true;
}

void RequestStream::HandleFrame(std::shared_ptr<IFrame> frame) {
    switch (static_cast<FrameType>(frame->GetType())) {
        case FrameType::kPushPromise:
            HandlePushPromise(frame);
            break;
        default:
            ReqRespBaseStream::HandleFrame(frame);
            break;
    }
}

void RequestStream::HandleBody() {
    std::shared_ptr<IResponse> response = std::make_shared<Response>();
    response->SetHeaders(headers_);
    response->SetBody(std::string(body_.begin(), body_.end())); // TODO: do not copy body

    PseudoHeader::Instance().DecodeResponse(response);
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
        error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
        return;
    }

    push_promise_handler_(headers, push_promise_frame->GetPushId());
}

}
}