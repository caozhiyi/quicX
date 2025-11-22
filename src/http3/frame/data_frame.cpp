#include "http3/frame/data_frame.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/log/log.h"

namespace quicx {
namespace http3 {

bool DataFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    uint32_t data_length = EvaluatePayloadSize();
    common::LOG_DEBUG("DataFrame::Encode: data_length:%u, buffer_length:%u", data_length, buffer->GetDataLength());
    // Check if we have enough data
    if (data_->GetDataLength() < data_length) {
        return false;
    }

    // Encode frame header in a separate scope
    common::BufferEncodeWrapper wrapper(buffer);

    // Encode frame type
    if (!wrapper.EncodeFixedUint16(type_)) {
        common::LOG_ERROR("DataFrame::Encode: failed to encode frame type");
        return false;
    }

    // Encode length
    if (!wrapper.EncodeVarint(data_length)) {
        common::LOG_ERROR("DataFrame::Encode: failed to encode length");
        return false;
    }

    // Flush will commit the encoded header to buffer
    wrapper.Flush();
    common::LOG_DEBUG("DataFrame::Encode: buffer_length:%u", data_length, buffer->GetDataLength());

    // Now write the data payload
    auto write_buffer = data_->CloneReadable(data_length);
    if (!write_buffer) {
        common::LOG_ERROR("DataFrame::Encode: failed to clone readable");
        return false;
    }
    buffer->Write(write_buffer);
    common::LOG_DEBUG("DataFrame::Encode: buffer_length:%u", data_length, buffer->GetDataLength());
    return true;
}

DecodeResult DataFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::BufferDecodeWrapper wrapper(buffer);

    // Decode frame type if needed
    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            common::LOG_ERROR("DataFrame::Decode: failed to decode frame type");
            return DecodeResult::kError;
        }
    }

    // Decode length
    if (!wrapper.DecodeVarint(length_)) {
        common::LOG_ERROR("DataFrame::Decode: failed to decode length");
        return DecodeResult::kError;
    }

    // Check if we have enough data for the payload
    if (buffer->GetDataLength() < length_) {
        wrapper.CancelDecode();
        common::LOG_DEBUG("DataFrame::Decode: insufficient data (need %u, have %u), waiting for more", length_,
            buffer->GetDataLength());
        return DecodeResult::kNeedMoreData;  // Not an error, just need more data
    }
    wrapper.Flush();
    // Use CloneReadable helper to extract exactly length_ bytes and advance read
    // pointer This works correctly for both SingleBlockBuffer and
    // MultiBlockBuffer
    data_ = buffer->CloneReadable(length_);
    if (!data_) {
        return DecodeResult::kError;
    }

    return DecodeResult::kSuccess;
}

uint32_t DataFrame::EvaluateEncodeSize() {
    uint32_t size = 0;

    // Frame type size
    size += sizeof(type_);
    ;

    // Data size (this also updates length_ if it's 0)
    size += EvaluatePayloadSize();

    // Length field size (must be after EvaluatePayloadSize)
    size += common::GetEncodeVarintLength(length_);

    return size;
}

uint32_t DataFrame::EvaluatePayloadSize() {
    if (length_ == 0) {
        length_ = data_->GetDataLength();
    }
    return length_;
}

}  // namespace http3
}  // namespace quicx
