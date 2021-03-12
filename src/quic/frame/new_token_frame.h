#ifndef QUIC_FRAME_NEW_TOKEN_FRAME
#define QUIC_FRAME_NEW_TOKEN_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class NewTokenFrame: public Frame {
public:
    NewTokenFrame(): Frame(FT_NEW_TOKEN) {}
    ~NewTokenFrame() {}

private:
    uint32_t _length;  // the length of the token in bytes.
    char* _data;       // An opaque blob that the client may use with a future Initial packet.
};

}

#endif