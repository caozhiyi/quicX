#ifndef QUIC_STREAM_FRAME_VISITOR_INTERFACE
#define QUIC_STREAM_FRAME_VISITOR_INTERFACE

#include <vector>
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class IFrameVisitor {
public:
    IFrameVisitor() {}
    virtual ~IFrameVisitor() {}

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) = 0;

    virtual uint32_t GetLeftSize() = 0;

    virtual std::shared_ptr<IBuffer> GetBuffer() = 0;

    virtual std::vector<FrameType>& GetFramesType() = 0;
};


}

#endif