#ifndef QUIC_FRAME_PING_FRAME
#define QUIC_FRAME_PING_FRAME

#include "quic/frame/frame_interface.h"

namespace quicx {

class PingFrame:
    public IFrame {
public:
    PingFrame(): IFrame(FT_PING) {}
    ~PingFrame() {}
};

}

#endif