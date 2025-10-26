#include "common/log/log.h"
#include "common/buffer/buffer_read_view.h"
#include "http3/stream/unidentified_stream.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

UnidentifiedStream::UnidentifiedStream(
    const std::shared_ptr<quic::IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const StreamTypeCallback& type_callback)
    : IStream(error_handler),
      stream_(stream),
      type_callback_(type_callback),
      type_identified_(false) {
    
    stream_->SetStreamReadCallBack(
        std::bind(&UnidentifiedStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
    
    common::LOG_DEBUG("UnidentifiedStream created for stream %llu", stream_->GetStreamID());
}

UnidentifiedStream::~UnidentifiedStream() {
    // Don't close the stream - it will be handed off to the real stream object
}

void UnidentifiedStream::OnData(std::shared_ptr<common::IBufferRead> data, uint32_t error) {
    if (error != 0) {
        common::LOG_ERROR("UnidentifiedStream::OnData error: %d on stream %llu", error, stream_->GetStreamID());
        error_handler_(stream_->GetStreamID(), error);
        return;
    }

    if (type_identified_) {
        // Stream type already identified, shouldn't receive more data
        common::LOG_WARN("UnidentifiedStream: received data after type identified on stream %llu", 
                        stream_->GetStreamID());
        return;
    }

    // Append data to buffer
    auto span = data->GetReadSpan();
    size_t data_len = span.GetEnd() - span.GetStart();
    buffer_.insert(buffer_.end(), span.GetStart(), span.GetEnd());
    
    common::LOG_DEBUG("UnidentifiedStream: received %zu bytes, buffer size now = %zu on stream %llu", 
                     data_len, buffer_.size(), stream_->GetStreamID());

    // Try to read stream type
    TryReadStreamType();
}

bool UnidentifiedStream::TryReadStreamType() {
    if (buffer_.empty()) {
        return false;
    }

    // Try to decode varint for stream type
    auto buffer_view = std::make_shared<common::BufferReadView>(buffer_.data(), buffer_.size());
    uint64_t stream_type = 0;
    
    {
        common::BufferDecodeWrapper wrapper(buffer_view);
        if (!wrapper.DecodeVarint(stream_type)) {
            // Not enough data yet, wait for more
            common::LOG_DEBUG("UnidentifiedStream: not enough data to read stream type on stream %llu (buffer size=%zu)", 
                            stream_->GetStreamID(), buffer_.size());
            return false;
        }
        // Wrapper will flush on destruction, moving read pointer
    }

    // Calculate how many bytes were consumed
    size_t consumed = buffer_.size() - buffer_view->GetDataLength();
    common::LOG_DEBUG("UnidentifiedStream: stream type %llu identified on stream %llu, consumed %zu bytes", 
                     stream_type, stream_->GetStreamID(), consumed);

    // Extract remaining data
    std::vector<uint8_t> remaining_data;
    if (consumed < buffer_.size()) {
        remaining_data.assign(buffer_.begin() + consumed, buffer_.end());
        common::LOG_DEBUG("UnidentifiedStream: %zu bytes remaining after stream type on stream %llu", 
                         remaining_data.size(), stream_->GetStreamID());
    }

    type_identified_ = true;

    // Unregister callback to prevent receiving more data
    stream_->SetStreamReadCallBack(nullptr);

    // Notify callback with stream type and remaining data
    if (type_callback_) {
        type_callback_(stream_type, stream_, remaining_data);
    }

    return true;
}

}
}

