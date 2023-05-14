#ifndef QUIC_PROCESS_DATA_VISITOR_INTERFACE
#define QUIC_PROCESS_DATA_VISITOR_INTERFACE

#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class IDataVisitor {
public:
    IDataVisitor() {}
    virtual ~IDataVisitor() {}

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) = 0;

    virtual uint32_t GetLeftSize() = 0;

    virtual std::shared_ptr<IBuffer> GetBuffer() = 0;
};


}

#endif