#include "common/log/log.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/push_stream.h"

namespace quicx {
namespace http3 {

PushStream::PushStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(int32_t)>& error_handler,
    uint64_t push_id):
    IStream(error_handler),
    qpack_encoder_(qpack_encoder),
    stream_(stream),
    push_id_(push_id) {

}

PushStream::~PushStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool PushStream::SendPushResponse(const std::unordered_map<std::string, std::string>& headers,
                                const std::string& body) {
    // Encode headers using qpack
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(headers, headers_buffer)) {
        common::LOG_ERROR("PushStream::SendPushResponse qpack encode error");
        return false;
    }

    // Send HEADERS frame
    HeadersFrame headers_frame;
    std::vector<uint8_t> encoded_fields(headers_buffer->GetData(), headers_buffer->GetData() + headers_buffer->GetDataLength());
    headers_frame.SetEncodedFields(encoded_fields);

    uint8_t frame_buf[4096]; // TODO: Use dynamic buffer
    auto frame_buffer = std::make_shared<common::Buffer>(frame_buf, sizeof(frame_buf));
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("PushStream::SendPushResponse headers frame encode error");
        return false;
    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("PushStream::SendPushResponse send headers error");
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
            common::LOG_ERROR("PushStream::SendPushResponse data frame encode error");
            return false;
        }
        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("PushStream::SendPushResponse send data error");
            return false;
        }
    }

    return true;
}

}
}
