#include "common/decode/decode.h"
#include "common/log/log.h"
#include "http3/frame/headers_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/multi_block_buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {

bool HeadersFrame::Encode(std::shared_ptr<common::IBuffer> buffer) {
    common::BufferEncodeWrapper wrapper(buffer);
    // Write frame type
    if (!wrapper.EncodeFixedUint16(type_)) {
        return false;
    }

    // Write length
    uint32_t payload_size = EvaluatePayloadSize();
    if (!wrapper.EncodeVarint(payload_size)) {
        return false;
    }
    wrapper.Flush();

    // Write only the specified length of encoded fields
    if (payload_size > 0) {
        uint32_t encoded_length = encoded_fields_->GetDataLength();

        if (encoded_length < payload_size) {
            common::LOG_ERROR(
                "HeadersFrame::Encode: encoded_fields length (%u) < payload_size (%u)", encoded_length, payload_size);
            return false;
        }

        // Use CloneReadable to get the exact payload and advance read pointer
        auto payload = encoded_fields_->CloneReadable(payload_size);
        if (!payload) {
            common::LOG_ERROR("HeadersFrame::Encode: CloneReadable failed for length %u", payload_size);
            return false;
        }
        buffer->Write(payload);
    }
    return true;
}

DecodeResult HeadersFrame::Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type) {
    common::MultiBlockBufferDecodeWrapper wrapper(buffer);

    if (with_type) {
        if (!wrapper.DecodeFixedUint16(type_)) {
            return DecodeResult::kError;
        }
    }

    // Read length
    if (!wrapper.DecodeVarint(length_)) {
        return DecodeResult::kError;
    }

    // Check if we have enough data
    if (wrapper.GetDataLength() < length_) {
        wrapper.CancelDecode();
        common::LOG_DEBUG("HeadersFrame::Decode: insufficient data (need %u, have %u), waiting for more", length_,
            wrapper.GetDataLength());
        return DecodeResult::kNeedMoreData;  // Not an error, just need more data
    }

    wrapper.Flush();
    // Read encoded fields - shallow copy with specified length
    // CloneReadable creates a shallow copy and advances the read pointer
    encoded_fields_ = wrapper.GetBuffer()->CloneReadable(length_);
    if (!encoded_fields_) {
        common::LOG_ERROR("HeadersFrame::Decode: CloneReadable failed for length %u", length_);
        return DecodeResult::kError;
    }

    return DecodeResult::kSuccess;
}

uint32_t HeadersFrame::EvaluateEncodeSize() {
    uint32_t size = 0;

    // Size for frame type
    size += sizeof(type_);

    // Size for length field
    size += common::GetEncodeVarintLength(length_);

    // Size for encoded fields
    size += EvaluatePayloadSize();

    return size;
}

uint32_t HeadersFrame::EvaluatePayloadSize() {
    if (length_ == 0) {
        length_ = encoded_fields_->GetDataLength();
    }
    return length_;
}

}  // namespace http3
}  // namespace quicx
