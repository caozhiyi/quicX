#ifndef COMMON_BUFFER_BUFFER_DECODE_WRAPPER
#define COMMON_BUFFER_BUFFER_DECODE_WRAPPER

#include <memory>
#include <cstdint>
#include "common/decode/decode.h"
#include "common/buffer/if_buffer_read.h"

namespace quicx {
namespace common {

/**
 * @brief Buffer decode wrapper
 * 
 * The buffer decode wrapper is used to decode the data from the buffer.
 * It is responsible for decoding the varint, fixed uint8, fixed uint16, fixed uint32, fixed uint64, and bytes.
 */
class BufferDecodeWrapper {
public:
    BufferDecodeWrapper(std::shared_ptr<IBufferRead> buffer);
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
    std::shared_ptr<IBufferRead> buffer_;
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

