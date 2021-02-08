#ifndef QUIC_FRAME_HANDSHAKE_DONE_FRAME
#define QUIC_FRAME_HANDSHAKE_DONE_FRAME

#include "frame_interface.h"

namespace quicx {

class HandshakeDoneFrame : public Frame {
public:
    HandshakeDoneFrame() : Frame(FT_HANDSHAKE_DONE) {}
    ~HandshakeDoneFrame() {}

};

}

#endif