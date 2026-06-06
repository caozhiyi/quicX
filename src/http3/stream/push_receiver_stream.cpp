#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/stream/type.h"
#include "http3/http/response.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/pseudo_header.h"
#include "http3/stream/push_receiver_stream.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/multi_block_buffer.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace http3 {

PushReceiverStream::PushReceiverStream(const std::shared_ptr<QpackEncoder>& qpack_decoder,
    const std::shared_ptr<IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const http_response_handler& response_handler):
    IRecvStream(StreamType::kPush, stream, error_handler),
    qpack_decoder_(qpack_decoder),
    response_handler_(response_handler),
    parse_state_(ParseState::kReadingPushId),
    push_id_(0),
    body_length_(0) {
    stream_->SetStreamReadCallBack(
        [this](auto a, auto b, auto c) { OnData(a, b, c); });
    // No send-side callback is registered here: a server push stream is, by
    // RFC 9114 §4.4 / §6.2.2, unidirectional from server to client, and on
    // the client side stream_ is therefore an IQuicRecvStream — there is no
    // send direction to wire up.
}

PushReceiverStream::~PushReceiverStream() {
    // Note: Do NOT call stream_->Reset() here during destruction.
    // See QpackDecoderSenderStream destructor comment for details.
    stream_.reset();
}

void PushReceiverStream::OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        LOG_ERROR("PushReceiverStream::OnData error: %d", error);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    LOG_DEBUG("=== PushReceiverStream::OnData: received %u bytes, current state=%d ===", data->GetDataLength(),
        static_cast<int>(parse_state_));

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
                    LOG_DEBUG("PushReceiverStream: not enough data to read push id, waiting for more data");
                    return;
                }
            }

            LOG_DEBUG("PushReceiverStream: push_id=%llu", push_id_);
            parse_state_ = ParseState::kReadingFrames;

        } else if (parse_state_ == ParseState::kReadingFrames) {
            // Decode HTTP3 frames (HEADERS + DATA)
            LOG_DEBUG("PushReceiverStream: attempting to decode frames from %zu bytes", data->GetDataLength());

            std::vector<std::shared_ptr<IFrame>> frames;
            bool decode_ok = frame_decoder_.DecodeFrames(buffer, frames);

            LOG_DEBUG(
                "PushReceiverStream: DecodeFrames returned %d, decoded %zu frames", decode_ok, frames.size());

            // Distinguish between incomplete frame (need more data) and actual decode error
            if (!decode_ok) {
                if (frames.empty() && buffer->GetDataLength() > 0) {
                    // No frames decoded but buffer has data: could be incomplete frame header.
                    // Only treat as "need more data" if the buffer is small enough to be a partial header.
                    // A reasonable max frame header size is 16 bytes (type varint + length varint).
                    if (buffer->GetDataLength() <= 16) {
                        LOG_DEBUG("PushReceiverStream: waiting for more data to complete frame");
                        return;
                    }
                }
                // Decode truly failed: either partial failure or corrupt data
                LOG_ERROR("PushReceiverStream: DecodeFrames failure (decoded %zu frames before error)", frames.size());
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
                        LOG_ERROR("PushReceiverStream: unexpected frame type %d", frame->GetType());
                        error_handler_(GetStreamID(), Http3ErrorCode::kFrameUnexpected);
                        return;
                }
            }
        }
    }
}

void PushReceiverStream::HandleHeaders(std::shared_ptr<IFrame> frame) {
    LOG_DEBUG("=== PushReceiverStream::HandleHeaders called ===");

    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    if (!headers_frame) {
        LOG_ERROR("IStream::HandleHeaders error");
        error_handler_(GetStreamID(), Http3ErrorCode::kFrameUnexpected);
        return;
    }

    // See ReqRespBaseStream::HandleHeaders for the rationale: HEADERS-frame
    // length is enforced by FrameDecoder / HeadersFrame::Decode, and QPACK
    // well-formedness is enforced by qpack_decoder_->Decode below. Push
    // streams reuse the same frame and QPACK layers, so no separate length
    // check is needed here either.

    // Decode headers using QPACK (decoder table for incoming headers)
    auto headers_buffer = headers_frame->GetEncodedFields();
    if (!qpack_decoder_->Decode(headers_buffer, headers_)) {
        LOG_ERROR("IStream::HandleHeaders QPACK decode error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }

    LOG_DEBUG("decoded %zu headers", headers_.size());

    if (headers_.find("content-length") != headers_.end()) {
        try {
            body_length_ = std::stoul(headers_["content-length"]);
            LOG_DEBUG("found content-length=%u", body_length_);
        } catch (const std::exception& e) {
            LOG_ERROR("PushReceiverStream: invalid content-length value '%s': %s",
                headers_["content-length"].c_str(), e.what());
            error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
            return;
        }
    } else {
        body_length_ = 0;
        LOG_DEBUG("no content-length, setting to 0");
    }

    if (body_length_ == 0) {
        LOG_DEBUG("body_length_==0, calling HandleBody()");
        HandleBody();
    }
}

void PushReceiverStream::HandleData(std::shared_ptr<IFrame> frame) {
    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    if (!data_frame) {
        LOG_ERROR("IStream::HandleData error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return;
    }

    // Initialize body_ if not already done
    if (!body_) {
        body_ = std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    }

    body_->Write(data_frame->GetData());

    if (body_length_ == body_->GetDataLength()) {
        HandleBody();
    }
}

void PushReceiverStream::HandleBody() {
    uint32_t body_size = body_ ? body_->GetDataLength() : 0;
    LOG_DEBUG("headers count=%zu, body size=%u", headers_.size(), body_size);

    std::shared_ptr<Response> response = std::make_shared<Response>();
    response->SetHeaders(headers_);
    if (body_) {
        response->SetBody(body_);
    }

    PseudoHeader::Instance().DecodeResponse(response);

    response_handler_(response, 0);
}

}  // namespace http3
}  // namespace quicx
