#ifndef QUIC_FRAME_FRAME_INTERFACE
#define QUIC_FRAME_FRAME_INTERFACE

#include <memory>
#include "type.h"

namespace quicx {

class Buffer;
class AlloterWrap;
class Frame {
public:
    Frame(FrameType ft);
    ~Frame();

    FrameType GetType();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t DecodeSize();
protected:
    FrameType _frame_type;
};

}

#endif