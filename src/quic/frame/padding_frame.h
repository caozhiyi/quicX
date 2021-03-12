#ifndef QUIC_FRAME_PADDING_FRAME
#define QUIC_FRAME_PADDING_FRAME

#include "frame_interface.h"

namespace quicx {

class PaddingFrame: public Frame {
public:
    PaddingFrame(): Frame(FT_PADDING) {}
    ~PaddingFrame() {}
};

}

#endif