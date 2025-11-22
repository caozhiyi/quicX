#include "common/log/log.h"
#include "http3/http/error.h"
#include "http3/stream/type.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/stream/pseudo_header.h"
#include "quic/quicx/global_resource.h"
#include "http3/stream/push_sender_stream.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {

PushSenderStream::PushSenderStream(const std::shared_ptr<QpackEncoder>& qpack_encoder,
    const std::shared_ptr<IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler):
    ISendStream(StreamType::kPush, stream, error_handler),
    push_id_(0),
    qpack_encoder_(qpack_encoder) {}

bool PushSenderStream::SendPushResponse(uint64_t push_id, std::shared_ptr<IResponse> response) {
    // RFC 9114 Section 4.6: Push stream format
    // Push Stream {
    //   Stream Type (i) = 0x01,
    //   Push ID (i),
    //   HTTP Message (..),
    // }

    // 1. Send Stream Type (Push stream type per RFC 9114)
    if (!EnsureStreamPreamble()) {
        return false;
    }

    // 2. Send Push ID (varint)
    auto id_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    {
        common::BufferEncodeWrapper id_wrapper(id_buffer);
        id_wrapper.EncodeVarint(push_id);
        // Wrapper will flush on destruction
    }

    // 3. Send HTTP Message (HEADERS + DATA)
    PseudoHeader::Instance().EncodeResponse(response);

    auto body = response->GetBody();
    size_t body_len = body ? body->GetDataLength() : 0;
    if (body && body_len > 0) {
        response->AddHeader("content-length", std::to_string(body_len));
    }

    // Encode headers using qpack
    auto headers_buffer =
        std::make_shared<common::MultiBlockBuffer>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!qpack_encoder_->Encode(response->GetHeaders(), headers_buffer)) {
        common::LOG_ERROR("qpack encode error");
        return false;
    }

    HeadersFrame headers_frame;
    headers_frame.SetEncodedFields(headers_buffer);
    auto frame_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    if (!headers_frame.Encode(frame_buffer)) {
        common::LOG_ERROR("encode headers frame error");
        error_handler_(GetStreamID(), Http3ErrorCode::kMessageError);
        return false;
    }
    if (!stream_->Flush()) {
        common::LOG_ERROR("send headers error");
        error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
        return false;
    }

    // Send DATA frame if body exists, TODO may send multiple DATA frames if body is large
    if (body && body_len > 0) {
        DataFrame data_frame;
        data_frame.SetData(std::dynamic_pointer_cast<common::IBuffer>(body));
        data_frame.SetLength(body_len);

        auto data_buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
        if (!data_frame.Encode(data_buffer)) {
            common::LOG_ERROR("encode data frame error");
            error_handler_(GetStreamID(), Http3ErrorCode::kInternalError);
            return false;
        }

        if (!stream_->Flush()) {
            common::LOG_ERROR("send data error");
            error_handler_(GetStreamID(), Http3ErrorCode::kClosedCriticalStream);
            return false;
        }
    }

    return true;
}

void PushSenderStream::Reset(uint32_t error_code) {
    if (stream_) {
        common::LOG_DEBUG("resetting stream %llu with error code %u", GetStreamID(), error_code);
        stream_->Reset(error_code);
    }
}

}  // namespace http3
}  // namespace quicx
