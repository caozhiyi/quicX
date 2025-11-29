#ifndef COMMON_BUFFER_MULTI_BLOCK_BUFFER_DECODE_WRAPPER
#define COMMON_BUFFER_MULTI_BLOCK_BUFFER_DECODE_WRAPPER

#include <sys/types.h>
#include <cstdint>
#include <memory>

#include "common/buffer/buffer_span.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/multi_block_buffer_read_view.h"
#include "common/decode/decode.h"

namespace quicx {
namespace common {

// MultiBlockBufferDecodeWrapper decodes primitive types from an IBuffer using
// IBuffer's interface semantics. Unlike BufferDecodeWrapper which requires a
// contiguous span, this wrapper can handle multi-block buffers by using
// ReadNotMovePt and MoveReadPt to work across block boundaries.
// It stages progress and advances the underlying buffer only when Flush() is
// invoked, allowing callers to roll back on failure without mutating buffer state.
class MultiBlockBufferDecodeWrapper {
public:
    // |buffer| must outlive the wrapper.
    MultiBlockBufferDecodeWrapper(std::shared_ptr<IBuffer> buffer);
    ~MultiBlockBufferDecodeWrapper();

    // flush the buffer
    void Flush();
    // cancel all decode, reset buffer read pos
    void CancelDecode();

    // decode varint
    template <typename T>
    bool DecodeVarint(T& value);

    template <typename T>
    bool DecodeVarint(T& value, int32_t& len);

    bool DecodeFixedUint8(uint8_t& value);
    bool DecodeFixedUint16(uint16_t& value);
    bool DecodeFixedUint32(uint32_t& value);
    bool DecodeFixedUint64(uint64_t& value);

    common::BufferSpan GetDataSpan() const;
    uint32_t GetDataLength() const;
    uint32_t GetReadLength() const;
    
    // Get the underlying buffer for operations like CloneReadable()
    std::shared_ptr<IBuffer> GetBuffer() const;

private:
    uint32_t GetConsumedOffset() const;
    // Read data for decoding operations, handling multi-block boundaries
    // Returns true if enough data was read, false otherwise
    bool ReadForDecode(uint8_t* temp_buf, uint32_t needed_len, uint32_t& actual_len);

    std::shared_ptr<IBuffer> buffer_;
    MultiBlockBufferReadView read_view_;  // View to read from buffer without modifying its read pointer
    bool flushed_;
};

template <typename T>
bool MultiBlockBufferDecodeWrapper::DecodeVarint(T& value) {
    // Varint can be up to 8 bytes. We need to read enough bytes to decode.
    // First, check how much data is available
    uint32_t available = read_view_.GetDataLength();
    if (available == 0) {
        return false;
    }
    
    // Read up to 8 bytes (max varint size) or all available data
    uint32_t to_read = (available > 8) ? 8 : available;
    uint8_t varint_buf[8];
    uint32_t actual_len = 0;
    
    if (!ReadForDecode(varint_buf, to_read, actual_len)) {
        return false;
    }
    
    // Try to decode varint
    uint8_t* varint_end = varint_buf + actual_len;
    uint8_t* decoded_end = common::DecodeVarint(varint_buf, varint_end, value);
    
    if (decoded_end == nullptr) {
        // DecodeVarint failed - might need more data
        // Check if we have more data available
        uint32_t remaining = read_view_.GetDataLength();
        if (actual_len < 8 && remaining > actual_len) {
            // Try reading more
            uint32_t more_to_read = (remaining > 8) ? 8 : remaining;
            if (more_to_read > actual_len) {
                if (ReadForDecode(varint_buf, more_to_read, actual_len)) {
                    varint_end = varint_buf + actual_len;
                    decoded_end = common::DecodeVarint(varint_buf, varint_end, value);
                }
            }
        }
        if (decoded_end == nullptr) {
            return false;
        }
    }

    // Calculate how many bytes were consumed
    uint32_t varint_bytes = decoded_end - varint_buf;
    read_view_.MoveReadPt(varint_bytes);
    flushed_ = false;
    return true;
}

template <typename T>
bool MultiBlockBufferDecodeWrapper::DecodeVarint(T& value, int32_t& len) {
    // Record the offset before decoding
    uint32_t offset_before = read_view_.GetReadOffset();
    
    if (!DecodeVarint(value)) {
        return false;
    }
    
    // Calculate how many bytes were consumed
    uint32_t offset_after = read_view_.GetReadOffset();
    uint32_t varint_bytes = offset_after - offset_before;
    
    // Update len to track remaining length
    len -= static_cast<int32_t>(varint_bytes);
    
    return true;
}

}  // namespace common
}  // namespace quicx

#endif

