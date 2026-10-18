#include "common/buffer/buffer_decode_wrapper.h"
#include "common/log/log.h"

#include "http3/stream/unidentified_stream.h"

namespace quicx {
namespace http3 {

UnidentifiedStream::UnidentifiedStream(const std::shared_ptr<IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const StreamTypeCallback& type_callback):
    IRecvStream(StreamType::kUnidentified, stream, error_handler),
    type_identified_(false),
    type_callback_(type_callback) {
    stream_->SetStreamReadCallBack([this](auto a, auto b, auto c) { OnData(a, b, c); });

    LOG_DEBUG("UnidentifiedStream created for stream %llu", stream_->GetStreamID());
}

void UnidentifiedStream::OnData(std::shared_ptr<IBufferRead> data, bool is_last, uint32_t error) {
    if (error != 0) {
        LOG_ERROR("UnidentifiedStream::OnData error: %d on stream %llu", error, stream_->GetStreamID());
        // The transport stream was reset: no further data can arrive. Unregister
        // the read callback and release this wrapper so both the HTTP/3 layer
        // and the transport can reclaim it.
        stream_->SetStreamReadCallBack(nullptr);
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    if (type_identified_) {
        // Stream type already identified, shouldn't receive more data
        LOG_WARN("UnidentifiedStream: received data after type identified on stream %llu", stream_->GetStreamID());
        return;
    }

    LOG_DEBUG("UnidentifiedStream: received %u bytes on stream %llu", (uint32_t)data->GetDataLength(),
        (uint64_t)stream_->GetStreamID());

    if (data->GetDataLength() < 1) {
        if (is_last) {
            // FIN before any type byte: the stream can never be identified
            // (RFC 9114 §6.2 requires a complete varint type). Release the
            // wrapper instead of retaining it forever.
            LOG_WARN("UnidentifiedStream: stream %llu fin before type byte", (uint64_t)stream_->GetStreamID());
            stream_->SetStreamReadCallBack(nullptr);
            error_handler_(stream_->GetStreamID(), 0);
            return;
        }
        LOG_ERROR("UnidentifiedStream: not enough data to read stream type on stream %llu", stream_->GetStreamID());
        return;
    }

    // Try to decode varint for stream type
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    uint64_t stream_type = 0;
    {
        common::BufferDecodeWrapper wrapper(buffer);
        if (!wrapper.DecodeVarint(stream_type)) {
            if (is_last) {
                // FIN in the middle of the type varint: the type can never be
                // completed. Release the wrapper instead of retaining it
                // forever.
                LOG_WARN("UnidentifiedStream: stream %llu fin inside type varint",
                    (uint64_t)stream_->GetStreamID());
                stream_->SetStreamReadCallBack(nullptr);
                error_handler_(stream_->GetStreamID(), 0);
                return;
            }
            // Not enough data yet, wait for more
            LOG_DEBUG("UnidentifiedStream: not enough data to read stream type on stream %llu (buffer size=%u)",
                (uint64_t)stream_->GetStreamID(), (uint32_t)data->GetDataLength());
            return;
        }
    }

    type_identified_ = true;

    // Unregister callback to prevent receiving more data
    stream_->SetStreamReadCallBack(nullptr);

    // Notify callback with stream type and remaining data (excluding stream type byte)
    if (type_callback_) {
        // Pass the buffer_view which has the read pointer moved past the stream type
        type_callback_(stream_type, stream_, data);
    }
}

}  // namespace http3
}  // namespace quicx
