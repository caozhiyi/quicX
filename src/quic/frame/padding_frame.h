#ifndef QUIC_FRAME_PADDING_FRAME
#define QUIC_FRAME_PADDING_FRAME

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class PaddingFrame:
    public IFrame {
public:
    PaddingFrame(): IFrame(FT_PADDING) {}
    ~PaddingFrame() {}
};

}
}

#endif