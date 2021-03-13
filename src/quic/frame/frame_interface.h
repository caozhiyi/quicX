#ifndef QUIC_FRAME_FRAME_INTERFACE
#define QUIC_FRAME_FRAME_INTERFACE

#include <memory>
#include "type.h"

namespace quicx {

class Buffer;
class AlloterWrap;
class Frame {
public:
    Frame(uint16_t ft);
    ~Frame();

    uint16_t GetType();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    virtual uint32_t EncodeSize();
protected:
    uint16_t _frame_type;
};

}

#endif