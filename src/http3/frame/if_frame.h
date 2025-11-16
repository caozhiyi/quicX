#ifndef HTTP3_FRAME_IF_FRAME
#define HTTP3_FRAME_IF_FRAME

#include <memory>
#include <cstdint>
#include "http3/frame/type.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace http3 {

class IFrame {
public:
    IFrame(FrameType ft = FrameType::kUnknown): type_(ft) {}
    virtual ~IFrame() {}

    uint16_t GetType() { return type_; }

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer) = 0;
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false) = 0;
    virtual uint32_t EvaluateEncodeSize() = 0;
    virtual uint32_t EvaluatePayloadSize() = 0;

protected:
    uint16_t type_;
};

}
}

#endif
