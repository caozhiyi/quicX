#ifndef HTTP3_FRAME_IF_FRAME
#define HTTP3_FRAME_IF_FRAME

#include <cstdint>
#include "http3/frame/type.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {
namespace http3 {

class IFrame {
public:
    IFrame(uint16_t ft = FT_UNKNOW): type_(ft) {}
    virtual ~IFrame() {}

    uint16_t GetType() { return type_; }

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer) = 0;
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false) = 0;
    virtual uint32_t EvaluateEncodeSize() = 0;
    virtual uint32_t EvaluatePaloadSize() = 0;

protected:
    uint16_t type_;
};

}
}

#endif
