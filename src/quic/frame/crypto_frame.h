#ifndef QUIC_FRAME_CRYPTO_FRAME
#define QUIC_FRAME_CRYPTO_FRAME

#include <cstdint>
#include "quic/frame/frame_interface.h"

namespace quicx {

class Buffer;
class CryptoFrame:
    public IFrame {
public:
    CryptoFrame();
    ~CryptoFrame();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetOffset(uint64_t offset) { _offset = offset; }
    uint64_t GetOffset() { return _offset; }

    void SetData(uint8_t* data, uint32_t length) { 
        _data = data;
        _length = length;
    }
    uint8_t* GetData() { return _data; }
    uint32_t GetLength() { return _length; }

    void SetEncryptionLevel(uint8_t level) { _encryption_level = level; }
    uint8_t GetEncryptionLevel() { return _encryption_level; }

private:
    uint64_t _offset;  // the byte offset in the stream for the data in this CRYPTO frame.
    uint32_t _length;  // the length of the Crypto Data field in this CRYPTO frame.
    uint8_t* _data;    // the cryptographic message data.

    uint8_t _encryption_level;
};

}

#endif