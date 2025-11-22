#include "common/log/log.h"
#include "http3/stream/if_send_stream.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {

bool ISendStream::EnsureStreamPreamble() {
    if (wrote_type_) {
        common::LOG_DEBUG("ISendStream::EnsureStreamPreamble: already wrote type, stream_id=%llu", stream_->GetStreamID());
        return true;
    }
    
    // Send stream type for Control Stream (RFC 9114 Section 6.2.1)
    auto buffer = std::dynamic_pointer_cast<common::IBuffer>(stream_->GetSendBuffer());
    common::LOG_DEBUG("ISendStream::EnsureStreamPreamble: before encoding, stream_id=%llu, stream_type=%u, buffer_length=%u", 
                     stream_->GetStreamID(), stream_type_, buffer ? buffer->GetDataLength() : 0);
    
    common::BufferEncodeWrapper wrapper(buffer);
    wrapper.EncodeVarint(static_cast<uint64_t>(stream_type_));

    // Log buffer state after encoding stream type
    if (buffer && buffer->GetDataLength() > 0) {
        auto span = buffer->GetReadableSpan();
        std::string hex;
        uint32_t log_len = span.GetLength() < 16 ? span.GetLength() : 16;
        for (uint32_t i = 0; i < log_len; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", static_cast<uint8_t>(span.GetStart()[i]));
            hex += buf;
        }
        common::LOG_DEBUG("ISendStream::EnsureStreamPreamble: after encoding stream type, buffer length=%u, hex=[%s], stream_id=%llu", 
                         buffer->GetDataLength(), hex.c_str(), stream_->GetStreamID());
    }

    common::LOG_DEBUG("ISendStream::EnsureStreamPreamble: sent stream type on stream %llu", 
                     stream_->GetStreamID());
    wrote_type_ = true;
    return true;
}

}
}