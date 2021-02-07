#ifndef QUIC_FRAME_PING_FRAME
#define QUIC_FRAME_PING_FRAME

#include "frame_interface.h"

namespace quicx {

class PingFrame : public Frame {
public:
    PingFrame() : Frame(FT_PING) {}
    ~PingFrame() {}
};

}

#endif