#ifndef QUIC_FRAME_PADDING_FRAME
#define QUIC_FRAME_PADDING_FRAME

#include "quic/frame/frame_interface.h"

namespace quicx {

class PaddingFrame:
    public IFrame {
public:
    PaddingFrame(): IFrame(FT_PADDING) {}
    ~PaddingFrame() {}
};

}

#endif