#ifndef QUIC_FRAME_PING_FRAME
#define QUIC_FRAME_PING_FRAME

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class PingFrame:
    public IFrame {
public:
    PingFrame(): IFrame(FrameType::kPing) {}
    ~PingFrame() {}
};

}
}

#endif