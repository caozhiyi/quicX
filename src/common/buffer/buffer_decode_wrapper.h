#ifndef COMMON_BUFFER_BUFFER_DECODE_WRAPPER
#define COMMON_BUFFER_BUFFER_DECODE_WRAPPER

#include <memory>
#include <cstdint>
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace common {

// BufferDecodeWrapper iterates over readable data from an IBuffer and decodes
// primitive types using helpers from common/decode. It stages progress in
// place and advances the underlying buffer only when Flush() (or the
// destructor) is invoked, allowing callers to roll back on failure without
// mutating buffer state.
class BufferDecodeWrapper {
public:
    // |buffer| must outlive the wrapper.
    BufferDecodeWrapper(std::shared_ptr<IBuffer> buffer);
    ~BufferDecodeWrapper();

    // flush the buffer
    void Flush();

    // decode varint
    template<typename T>
    bool DecodeVarint(T& value);

    template<typename T>
    bool DecodeVarint(T& value, int32_t& len);

    bool DecodeFixedUint8(uint8_t& value);
    bool DecodeFixedUint16(uint16_t& value);
    bool DecodeFixedUint32(uint32_t& value);
    bool DecodeFixedUint64(uint64_t& value);
    bool DecodeBytes(uint8_t*& out, uint32_t len, bool copy = true);

    common::BufferSpan GetDataSpan() const;
    uint32_t GetDataLength() const;

private:
    std::shared_ptr<IBuffer> buffer_;
    uint8_t* pos_;
    uint8_t* end_;
    bool flushed_;
};

template<typename T>
bool BufferDecodeWrapper::DecodeVarint(T& value) {
    pos_ = common::DecodeVarint(pos_, end_, value);
    if (pos_ == nullptr) {
        return false;
    }
    flushed_ = false;
    return true;
}

template<typename T>
bool BufferDecodeWrapper::DecodeVarint(T& value, int32_t& len) {
    auto new_pos_ = common::DecodeVarint(pos_, end_, value);
    if (new_pos_ == nullptr) {
        return false;
    }
    flushed_ = false;
    len -= (new_pos_ - pos_);
    pos_ = new_pos_;
    return true;
}

}
}

#endif

