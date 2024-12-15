#ifndef QUIC_FRAME_IF_FRAME
#define QUIC_FRAME_IF_FRAME

#include <memory>
#include "type.h"

#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

class IFrame {
public:
    IFrame(uint16_t ft = FT_UNKNOW);
    virtual ~IFrame();

    uint16_t GetType();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();
    virtual uint32_t GetFrameTypeBit();

protected:
    uint16_t frame_type_;
};

}
}

#endif