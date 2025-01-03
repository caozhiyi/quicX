#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/response_stream.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/frame/push_promise_frame.h"

namespace quicx {
namespace http3 {

ResponseStream::ResponseStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicBidirectionStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const http_handler& http_handler):
    ReqRespBaseStream(qpack_encoder, stream, error_handler),
    http_handler_(http_handler) {

}

ResponseStream::~ResponseStream() {
    if (stream_) {
        stream_->Close();
    }
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
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
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
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
        return;
    }

    // Send frame to client
    stream_->Send(frame_buffer);
}

void ResponseStream::SendResponse(const std::shared_ptr<IResponse> response) {
    // send response
    // Decode request headers
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(response->GetHeaders(), headers_buffer)) {
        common::LOG_ERROR("ResponseStream::SendResponse error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
        return;
    }
    
    // Send HEADERS frame
    HeadersFrame response_headers_frame;
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    response_headers_frame.SetEncodedFields(encoded_fields);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!response_headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("ResponseStream::SendResponse error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
        return;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("ResponseStream::SendResponse error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_CLOSED_CRITICAL_STREAM);
        return;
    }

    // Send DATA frame if body exists
    if (response->GetBody().length() > 0) {
        DataFrame data_frame; // TODO: may send more than one DATA frame
        std::vector<uint8_t> body(response->GetBody().begin(), response->GetBody().end());
        data_frame.SetData(body);           

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("ResponseStream::SendResponse error");
            error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
            return;
        }
        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("ResponseStream::SendResponse error");
            error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_CLOSED_CRITICAL_STREAM);
            return;
        }
    }
}

void ResponseStream::HandleBody() {
    std::unique_ptr<IRequest> request;
    request->SetHeaders(headers_);
    request->SetBody(std::string(body_.begin(), body_.end())); // TODO: do not copy body

    std::unique_ptr<IResponse> response;
    http_handler_(std::move(request), std::move(response));

    SendResponse(std::move(response));
}

}
}