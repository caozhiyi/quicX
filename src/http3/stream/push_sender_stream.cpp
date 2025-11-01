#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/stream/type.h"
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/push_sender_stream.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {

PushSenderStream::PushSenderStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    IStream(error_handler),
    push_id_(0),
    qpack_encoder_(qpack_encoder),  
    stream_(stream) {

}

PushSenderStream::~PushSenderStream() {
    if (stream_) {
        stream_->Close();
    }
}

bool PushSenderStream::SendPushResponse(uint64_t push_id, std::shared_ptr<IResponse> response) {
    // RFC 9114 Section 4.6: Push stream format
    // Push Stream {
    //   Stream Type (i) = 0x01,
    //   Push ID (i),
    //   HTTP Message (..),
    // }
    
    // 1. Send Stream Type (Push stream type per RFC 9114)
    uint8_t stream_type_buf[8];
    auto type_buffer = std::make_shared<common::Buffer>(stream_type_buf, stream_type_buf + sizeof(stream_type_buf));
    {
        common::BufferEncodeWrapper type_wrapper(type_buffer);
        type_wrapper.EncodeVarint(static_cast<uint64_t>(StreamType::kPush));
        // Wrapper will flush on destruction
    }
    
    if (type_buffer->GetDataLength() > 0) {
        int32_t sent = stream_->Send(type_buffer);
        if (sent < 0) {
            common::LOG_ERROR("PushSenderStream::SendPushResponse send stream type failed");
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
    }
    
    // 2. Send Push ID (varint)
    uint8_t push_id_buf[16];
    auto id_buffer = std::make_shared<common::Buffer>(push_id_buf, push_id_buf + sizeof(push_id_buf));
    {
        common::BufferEncodeWrapper id_wrapper(id_buffer);
        id_wrapper.EncodeVarint(push_id);
        // Wrapper will flush on destruction
    }
    
    if (id_buffer->GetDataLength() > 0) {
        int32_t sent = stream_->Send(id_buffer);
        if (sent < 0) {
            common::LOG_ERROR("PushSenderStream::SendPushResponse send push ID failed");
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
    }
    
    // 3. Send HTTP Message (HEADERS + DATA)
    PseudoHeader::Instance().EncodeResponse(response);

    if (!response->GetBody().empty()) {
        response->AddHeader("content-length", std::to_string(response->GetBody().size()));
    }

    // Encode headers using qpack
    uint8_t headers_buf[4096]; // TODO: Use dynamic buffer
    auto headers_buffer = std::make_shared<common::Buffer>(headers_buf, sizeof(headers_buf));
    if (!qpack_encoder_->Encode(response->GetHeaders(), headers_buffer)) {
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
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return false;

    }
    if (stream_->Send(frame_buffer) <= 0) {
        common::LOG_ERROR("PushSenderStream::SendPushResponse send headers error");
        error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
        return false;
    }


    // Send DATA frame if body exists
    if (!response->GetBody().empty()) {
        DataFrame data_frame;
        std::vector<uint8_t> body_data(response->GetBody().begin(), response->GetBody().begin() + response->GetBody().size());
        data_frame.SetData(body_data);

        uint8_t data_buf[4096]; // TODO: Use dynamic buffer
        auto data_buffer = std::make_shared<common::Buffer>(data_buf, sizeof(data_buf));
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("PushSenderStream::SendPushResponse data frame encode error");
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }

        if (stream_->Send(data_buffer) <= 0) {
            common::LOG_ERROR("PushSenderStream::SendPushResponse send data error");
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
    }

    return true;
}

void PushSenderStream::Reset(uint32_t error_code) {
    if (stream_) {
        common::LOG_DEBUG("PushSenderStream::Reset: resetting stream %llu with error code %u", 
                         GetStreamID(), error_code);
        stream_->Reset(error_code);
    }
}

}
}
