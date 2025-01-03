#ifndef QUIC_FRAME_HANDSHAKE_DONE_FRAME
#define QUIC_FRAME_HANDSHAKE_DONE_FRAME

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class HandshakeDoneFrame:
    public IFrame {
public:
    HandshakeDoneFrame(): IFrame(FT_HANDSHAKE_DONE) {}
    ~HandshakeDoneFrame() {}
};

}
}

#endif