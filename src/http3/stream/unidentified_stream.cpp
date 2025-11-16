#include "common/log/log.h"
#include "http3/stream/unidentified_stream.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

UnidentifiedStream::UnidentifiedStream(
    const std::shared_ptr<IQuicRecvStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler,
    const StreamTypeCallback& type_callback):
    IRecvStream(StreamType::kUnidentified, stream, error_handler),
    type_identified_(false),
    type_callback_(type_callback) {
    
    stream_->SetStreamReadCallBack(
        std::bind(&UnidentifiedStream::OnData, this, std::placeholders::_1, std::placeholders::_2));
    
    common::LOG_DEBUG("UnidentifiedStream created for stream %llu", stream_->GetStreamID());
}

void UnidentifiedStream::OnData(std::shared_ptr<IBufferRead> data, uint32_t error) {
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

    common::LOG_DEBUG("UnidentifiedStream: received %zu bytes, buffer size now = %zu on stream %llu", 
                     data->GetDataLength(), stream_->GetStreamID());

    if (data->GetDataLength() < 1) {
        common::LOG_ERROR("UnidentifiedStream: not enough data to read stream type on stream %llu", 
                          stream_->GetStreamID());
        return;
    }

    // Try to decode varint for stream type
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(data);
    uint64_t stream_type = 0;
    {
        common::BufferDecodeWrapper wrapper(buffer);
        if (!wrapper.DecodeVarint(stream_type)) {
            // Not enough data yet, wait for more
            common::LOG_DEBUG("UnidentifiedStream: not enough data to read stream type on stream %llu (buffer size=%zu)", 
                                stream_->GetStreamID(), data->GetDataLength());
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

}
}

