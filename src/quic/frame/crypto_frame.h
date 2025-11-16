#ifndef QUIC_FRAME_CRYPTO_FRAME
#define QUIC_FRAME_CRYPTO_FRAME

#include <cstdint>
#include "quic/frame/if_frame.h"
#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace quic {

class Buffer;
class CryptoFrame:
    public IFrame {
public:
    CryptoFrame();
    ~CryptoFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetOffset(uint64_t offset) { offset_ = offset; }
    uint64_t GetOffset() { return offset_; }

    void SetData(common::SharedBufferSpan data) { 
        data_ = data;
        length_ = data.GetLength();
    }
    common::SharedBufferSpan GetData() { return data_; }
    uint32_t GetLength() { return length_; }

    void SetEncryptionLevel(uint8_t level) { encryption_level_ = level; }
    uint8_t GetEncryptionLevel() { return encryption_level_; }

private:
    uint64_t offset_;  // the byte offset in the stream for the data in this CRYPTO frame.
    uint32_t length_;  // the length of the Crypto Data field in this CRYPTO frame.
    common::SharedBufferSpan data_;    // the cryptographic message data.

    uint8_t encryption_level_;
};

}
}

#endif