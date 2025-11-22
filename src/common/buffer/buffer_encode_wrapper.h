#ifndef COMMON_BUFFER_BUFFER_ENCODE_WRAPPER
#define COMMON_BUFFER_BUFFER_ENCODE_WRAPPER

#include <memory>
#include <cstdint>
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace common {

// BufferEncodeWrapper is a small helper that writes primitive types into an
// IBuffer using the encoding routines from common/decode. It manages the write
// pointer lazily: data is staged in-place and committed (by moving the write
// pointer) when Flush() is invoked or when the wrapper is destroyed. This
// allows batching several writes without repeatedly touching the underlying
// buffer object.
class BufferEncodeWrapper {
public:
    // |buffer| must outlive the wrapper. The constructor acquires the current
    // writable span once; subsequent writes operate inside that window.
    BufferEncodeWrapper(std::shared_ptr<IBuffer> buffer);
    ~BufferEncodeWrapper();

    // flush the buffer
    void Flush();

    // encode varint
    template <typename T>
    bool EncodeVarint(T value);

    bool EncodeFixedUint8(uint8_t value);
    bool EncodeFixedUint16(uint16_t value);
    bool EncodeFixedUint32(uint32_t value);
    bool EncodeFixedUint64(uint64_t value);
    bool EncodeBytes(uint8_t* in, uint32_t len);

    // Return a span describing the portion written since construction/last
    // flush. Useful for logging and debug assertions.
    common::BufferSpan GetDataSpan() const;
    uint32_t GetDataLength() const;

private:
    std::shared_ptr<IBuffer> buffer_;
    uint8_t* start_;  // Save the start position from constructor
    uint8_t* pos_;
    uint8_t* end_;
    bool flushed_;
};

template <typename T>
bool BufferEncodeWrapper::EncodeVarint(T value) {
    if (pos_ >= end_) {
        return false;
    }

    uint8_t* new_pos = common::EncodeVarint(pos_, end_, value);
    if (new_pos == nullptr || new_pos > end_) {
        return false;
    }

    pos_ = new_pos;
    flushed_ = false;
    return true;
}

}  // namespace common
}  // namespace quicx

#endif
