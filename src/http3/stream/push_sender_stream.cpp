#include "common/log/log.h"
#include "http3/http/error.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/push_sender_stream.h"

namespace quicx {
namespace http3 {

PushSenderStream::PushSenderStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    uint64_t push_id):
    IStream(error_handler),
    qpack_encoder_(qpack_encoder),
    stream_(stream),
    push_id_(push_id) {

}

PushSenderStream::~PushSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool PushSenderStream::SendPushResponse(const std::unordered_map<std::string, std::string>& headers,
                                const std::string& body) {
    // Encode headers using qpack
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        common::LOG_ERROR("PushSenderStream::SendPushResponse qpack encode error");
        return false;
    }

    // Send HEADERS frame
    HeadersFrame headers_frame;
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    headers_frame.SetEncodedFields(encoded_fields);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("PushSenderStream::SendPushResponse headers frame encode error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_MESSAGE_ERROR);
        return false;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("PushSenderStream::SendPushResponse send headers error");
        error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_CLOSED_CRITICAL_STREAM);
        return false;
    }

    // Send DATA frame if body exists
    if (!body.empty()) {
        DataFrame data_frame;
        std::vector<uint8_t> body_data(body.begin(), body.end());
        data_frame.SetData(body_data);

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("PushSenderStream::SendPushResponse data frame encode error");
            error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_INTERNAL_ERROR);
            return false;
        }
        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("PushSenderStream::SendPushResponse send data error");
            error_handler_(GetStreamID(), HTTP3_ERROR_CODE::H3EC_CLOSED_CRITICAL_STREAM);
            return false;
        }
    }

    return true;
}

}
}