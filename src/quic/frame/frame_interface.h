#ifndef QUIC_FRAME_FRAME_INTERFACE
#define QUIC_FRAME_FRAME_INTERFACE

#include <memory>
#include "type.h"

#include "common/buffer/buffer_readonly.h"
#include "common/buffer/buffer_writeonly.h"

namespace quicx {

class AlloterWrap;
class IFrame {
public:
    IFrame(uint16_t ft = FT_UNKNOW);
    virtual ~IFrame();

    uint16_t GetType();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();
protected:
    uint16_t _frame_type;
};

}

#endif