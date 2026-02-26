#ifndef COMMON_BUFFER_BUFFER_DECODE_WRAPPER
#define COMMON_BUFFER_BUFFER_DECODE_WRAPPER

#include <cstdint>
#include <memory>

#include "common/buffer/buffer_span.h"
#include "common/buffer/buffer_reader.h"
#include "common/buffer/if_buffer.h"
#include "common/decode/decode.h"

namespace quicx {
namespace common {

// BufferDecodeWrapper decodes primitive types from an IBuffer. It supports two
// internal paths:
//   - Fast path (single-chunk buffer): direct pointer arithmetic on the
//     readable span, identical to the legacy BufferDecodeWrapper. Zero overhead.
//   - Slow path (multi-chunk buffer): reads through a BufferReader with
//     temporary stack buffers for cross-block decoding.
//
// Progress is staged: the underlying buffer's read pointer is only advanced
// when Flush() (or the destructor) is called, allowing rollback via
// CancelDecode().
class BufferDecodeWrapper {
public:
    explicit BufferDecodeWrapper(std::shared_ptr<IBuffer> buffer);
    ~BufferDecodeWrapper();

    // Commit consumed bytes to the underlying buffer.
    void Flush();
    // Discard progress; the buffer is not modified.
    void CancelDecode();

    // Varint decoding
    template <typename T>
    bool DecodeVarint(T& value);

    template <typename T>
    bool DecodeVarint(T& value, int32_t& len);

    // Fixed-width decoding
    bool DecodeFixedUint8(uint8_t& value);
    bool DecodeFixedUint16(uint16_t& value);
    bool DecodeFixedUint32(uint32_t& value);
    bool DecodeFixedUint64(uint64_t& value);

    // Byte array decoding (fast path only for no-copy; slow path always copies)
    bool DecodeBytes(uint8_t*& out, uint32_t len, bool copy = true);

    // Data span from start to current decode position (fast path only, empty for slow path)
    BufferSpan GetDataSpan() const;
    // Remaining decodable bytes
    uint32_t GetDataLength() const;
    // Bytes consumed since construction / last Flush
    uint32_t GetReadLength() const;

    // Access the underlying buffer (used by HTTP/3 frames for CloneReadable)
    std::shared_ptr<IBuffer> GetBuffer() const;

private:
    // Slow-path helper: read bytes into temp_buf via reader_
    bool SlowReadForDecode(uint8_t* temp_buf, uint32_t needed, uint32_t& actual);

    std::shared_ptr<IBuffer> buffer_;
    bool is_contiguous_;
    bool flushed_;

    // Fast path state (single-chunk)
    uint8_t* start_;
    uint8_t* pos_;
    uint8_t* end_;

    // Slow path state (multi-chunk)
    BufferReader reader_;
};

// ---- Template implementations ----

template <typename T>
bool BufferDecodeWrapper::DecodeVarint(T& value) {
    if (is_contiguous_) {
        uint8_t* new_pos = common::DecodeVarint(pos_, end_, value);
        if (new_pos == nullptr) {
            return false;
        }
        flushed_ = false;
        pos_ = new_pos;
        return true;
    }

    // Slow path: read up to 8 bytes into a stack buffer
    uint32_t available = reader_.GetDataLength();
    if (available == 0) {
        return false;
    }

    uint32_t to_read = (available > 8) ? 8 : available;
    uint8_t varint_buf[8];
    uint32_t actual_len = 0;

    if (!SlowReadForDecode(varint_buf, to_read, actual_len)) {
        return false;
    }

    uint8_t* varint_end = varint_buf + actual_len;
    uint8_t* decoded_end = common::DecodeVarint(varint_buf, varint_end, value);

    if (decoded_end == nullptr) {
        return false;
    }

    uint32_t varint_bytes = static_cast<uint32_t>(decoded_end - varint_buf);
    reader_.MoveReadPt(varint_bytes);
    flushed_ = false;
    return true;
}

template <typename T>
bool BufferDecodeWrapper::DecodeVarint(T& value, int32_t& len) {
    if (is_contiguous_) {
        uint8_t* before = pos_;
        uint8_t* new_pos = common::DecodeVarint(pos_, end_, value);
        if (new_pos == nullptr) {
            return false;
        }
        flushed_ = false;
        len -= static_cast<int32_t>(new_pos - before);
        pos_ = new_pos;
        return true;
    }

    // Slow path
    uint32_t offset_before = reader_.GetReadOffset();
    if (!DecodeVarint(value)) {
        return false;
    }
    uint32_t offset_after = reader_.GetReadOffset();
    len -= static_cast<int32_t>(offset_after - offset_before);
    return true;
}

}  // namespace common
}  // namespace quicx

#endif
