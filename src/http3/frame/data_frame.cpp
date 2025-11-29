#include "http3/frame/data_frame.h"
#include <cstring>  // for memcpy
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/multi_block_buffer_decode_wrapper.h"
#include "common/decode/decode.h"
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
    common::MultiBlockBufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            common::LOG_DEBUG("DataFrame::Decode: insufficient data for frame type");
            return DecodeResult::kNeedMoreData;
        }
    }

    // Read length
    uint64_t length_64 = 0;
    if (!wrapper.DecodeVarint(length_64)) {
        common::LOG_DEBUG("DataFrame::Decode: failed to decode length varint");
        return DecodeResult::kNeedMoreData;
    }

    length_ = static_cast<uint32_t>(length_64);

    // Check if we have enough data for the complete frame
    if (wrapper.GetDataLength() < length_) {
        wrapper.CancelDecode();
        common::LOG_DEBUG("DataFrame::Decode: insufficient data for complete frame (need %u, have %u)",
            length_, wrapper.GetDataLength());
        return DecodeResult::kNeedMoreData;
    }

    wrapper.Flush();
    // Extract payload - shallow copy with specified length
    // CloneReadable creates a shallow copy and advances the read pointer
    data_ = wrapper.GetBuffer()->CloneReadable(length_);
    if (!data_) {
        common::LOG_ERROR(
            "DataFrame::Decode: CloneReadable failed for length %u (buffer has %u)", length_, wrapper.GetBuffer()->GetDataLength());
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
