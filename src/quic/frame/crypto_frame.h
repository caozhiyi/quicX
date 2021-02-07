#ifndef QUIC_FRAME_CRYPTO_FRAME
#define QUIC_FRAME_CRYPTO_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class CryptoFrame : public Frame {
public:
    CryptoFrame() : Frame(FT_CRYPTO) {}
    ~CryptoFrame() {}

private:
    uint64_t _offset;  // the byte offset in the stream for the data in this CRYPTO frame.
    uint32_t _length;  // the length of the Crypto Data field in this CRYPTO frame.
    char* _data;       // the cryptographic message data.
};

}

#endif