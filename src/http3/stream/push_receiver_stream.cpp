#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/stream/type.h"
#include "http3/http/response.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/push_receiver_stream.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

PushReceiverStream::PushReceiverStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const http_response_handler& response_handler):
    IRecvStream(StreamType::kPush, stream, error_handler),
    qpack_encoder_(qpack_encoder),
    response_handler_(response_handler),
    parse_state_(ParseState::kReadingPushId),
    push_id_(0),
    body_length_(0) {

    stream_->SetStreamReadCallBack(std::bind(&PushReceiverStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
    // TODO send callback
}

PushReceiverStream::~PushReceiverStream() {
    if (stream_) {
        stream_->Reset(0);
    }
}

 void PushReceiverStream::OnData(std::shared_ptr<IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("PushReceiverStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    common::LOG_DEBUG("=== PushReceiverStream::OnData: received %u bytes, current state=%d ===", data->GetDataLength(), static_cast<int>(parse_state_));

    // State machine to parse push stream (RFC 9114 Section 4.6)
    // Note: Stream type byte (0x01) has already been read by UnidentifiedStream
    // So we start directly with Push ID
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    while (buffer->GetDataLength() > 0) {
        if (parse_state_ == ParseState::kReadingPushId) {
            // Read Push ID varint
            {
                common::BufferDecodeWrapper wrapper(buffer);
                if (!wrapper.DecodeVarint(push_id_)) {
                    // Not enough data yet, wait for more
                    common::LOG_DEBUG("PushReceiverStream: not enough data to read push id, waiting for more data");
                    return;
                }
            }

            common::LOG_DEBUG("PushReceiverStream: push_id=%llu", push_id_);
            parse_state_ = ParseState::kReadingFrames;

        } else if (parse_state_ == ParseState::kReadingFrames) {
            // Decode HTTP3 frames (HEADERS + DATA)
            common::LOG_DEBUG("PushReceiverStream: attempting to decode frames from %zu bytes", data->GetDataLength());
            
            std::vector<std::shared_ptr<IFrame>> frames;
            bool decode_ok = DecodeFrames(buffer, frames);

            common::LOG_DEBUG("PushReceiverStream: DecodeFrames returned %d, decoded %zu frames", decode_ok, frames.size());
            
            // If no frames were decoded and buffer still has data, wait for more
            if (!decode_ok && frames.empty()) {
                // Could be incomplete frame, wait for more data
                common::LOG_DEBUG("PushReceiverStream: waiting for more data to complete frame");
                return;
            }
            
            // If decoding failed after getting some frames, it's an error
            if (!decode_ok && !frames.empty()) {
                common::LOG_ERROR("PushReceiverStream: DecodeFrames partial failure");
                error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
                return;
            }

            // Process all decoded frames
            for (const auto& frame : frames) {
                switch (frame->GetType()) {
                case FrameType::kHeaders:
                    HandleHeaders(frame);
                    break;
                case FrameType::kData:
                    HandleData(frame);
                    break;
                default:
                    common::LOG_ERROR("PushReceiverStream: unexpected frame type %d", frame->GetType());
                    error_handler_(GetStreamID(), Http3ErrorCode::kFrameUnexpected);
                    return;
                }
            }
        }
    }
 }

 void PushReceiverStream::HandleHeaders(std::shared_ptr<IFrame> frame) {
    common::LOG_DEBUG("=== PushReceiverStream::HandleHeaders called ===");
    
    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    if (!headers_frame) {   
        common::LOG_ERROR("IStream::HandleHeaders error");
        error_handler_(GetStreamID(), Http3ErrorCode::kFrameUnexpected);
        return;
    }


    // TODO check if headers is complete and headers length is correct

    // Decode headers using QPACK
    auto headers_buffer = headers_frame->GetEncodedFields();
    if (!qpack_encoder_->Decode(headers_buffer, headers_)) {
        common::LOG_ERROR("IStream::HandleHeaders QPACK decode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }

    common::LOG_DEBUG("PushReceiverStream: decoded %zu headers", headers_.size());

    if (headers_.find("content-length") != headers_.end()) {
        body_length_ = std::stoul(headers_["content-length"]);
        common::LOG_DEBUG("PushReceiverStream: found content-length=%u", body_length_);
    } else {
        body_length_ = 0;
        common::LOG_DEBUG("PushReceiverStream: no content-length, setting to 0");
    }
    
    if (body_length_ == 0) {
        common::LOG_DEBUG("PushReceiverStream: body_length_==0, calling HandleBody()");
        HandleBody();
    }
 }

 void PushReceiverStream::HandleData(std::shared_ptr<IFrame> frame) {
    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    if (!data_frame) {
        common::LOG_ERROR("IStream::HandleData error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }
   
    body_->Write(data_frame->GetData());

    if (body_length_ == body_->GetDataLength()) {
        HandleBody();
    }
 }

 void PushReceiverStream::HandleBody() {
    uint32_t body_size = body_ ? body_->GetDataLength() : 0;
    common::LOG_DEBUG("PushReceiverStream: headers count=%zu, body size=%u", headers_.size(), body_size);
    
    std::shared_ptr<Response> response = std::make_shared<Response>();
    response->SetHeaders(headers_);
    if (body_) {
        response->SetBody(body_);
    }

    PseudoHeader::Instance().DecodeResponse(response);

    response_handler_(response, 0);
 }

}
}
