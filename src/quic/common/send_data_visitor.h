#ifndef QUIC_COMMON_DATA_VISITOR
#define QUIC_COMMON_DATA_VISITOR

#include "quic/frame/frame_interface.h"

namespace quicx {

class SendDataVisitor {
public:
    SendDataVisitor() {}
    virtual ~SendDataVisitor() {}

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) = 0;
};


}

#endif