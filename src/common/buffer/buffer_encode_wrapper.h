#ifndef COMMON_BUFFER_BUFFER_ENCODE_WRAPPER
#define COMMON_BUFFER_BUFFER_ENCODE_WRAPPER

#include <memory>
#include <cstdint>
#include "common/decode/decode.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace common {

/*
 * buffer encode wrapper,
 * it will flush the buffer when destruct
 */
class BufferEncodeWrapper {
public:
    BufferEncodeWrapper(std::shared_ptr<IBufferWrite> buffer);
    ~BufferEncodeWrapper();

    // flush the buffer
    void Flush();

    // encode varint
    template<typename T>
    bool EncodeVarint(T value);

    bool EncodeFixedUint8(uint8_t value);
    bool EncodeFixedUint16(uint16_t value);
    bool EncodeFixedUint32(uint32_t value);
    bool EncodeFixedUint64(uint64_t value);
    bool EncodeBytes(uint8_t* in, uint32_t len);

private:
    std::shared_ptr<IBufferWrite> buffer_;
    uint8_t* pos_;
    uint8_t* end_;
    bool flushed_;
};

template<typename T>
bool BufferEncodeWrapper::EncodeVarint(T value) {
    pos_ = common::EncodeVarint(pos_, value);
    if (pos_ == nullptr) {
        return false;
    }
    flushed_ = false;
    return true;
}

}
}

#endif
