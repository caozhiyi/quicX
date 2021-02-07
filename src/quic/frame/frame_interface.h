#ifndef QUIC_FRAME_FRAME_INTERFACE
#define QUIC_FRAME_FRAME_INTERFACE

#include "type.h"

namespace quicx {

class Frame {
public:
    Frame(FrameType ft) : _frame_type(ft) {}
    ~Frame() {}
protected:
    FrameType _frame_type;
};

}

#endif