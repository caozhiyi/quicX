#ifndef QUIC_FRAME_FRAME_INTERFACE
#define QUIC_FRAME_FRAME_INTERFACE

#include <memory>
#include "type.h"

#include "common/buffer/buffer_interface.h"

namespace quicx {

class IFrame {
public:
    IFrame(uint16_t ft = FT_UNKNOW);
    virtual ~IFrame();

    uint16_t GetType();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();
    virtual uint32_t GetFrameTypeBit();

protected:
    uint16_t _frame_type;
};

}

#endif