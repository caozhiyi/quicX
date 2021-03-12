#ifndef QUIC_FRAME_CRYPTO_FRAME
#define QUIC_FRAME_CRYPTO_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class Buffer;
class CryptoFrame: public Frame {
public:
    CryptoFrame();
    ~CryptoFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetOffset(uint64_t offset) { _offset = offset; }
    uint64_t GetOffset() { return _offset; }

    void SetData(std::shared_ptr<Buffer> data) { _data = data; }
    std::shared_ptr<Buffer> GetData() { return _data; }

private:
    uint64_t _offset;  // the byte offset in the stream for the data in this CRYPTO frame.
    std::shared_ptr<Buffer> _data;
    /*
    uint32_t _length;  // the length of the Crypto Data field in this CRYPTO frame.
    char* _data;       // the cryptographic message data.
    */
};

}

#endif