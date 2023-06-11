#ifndef QUIC_STREAM_FRAME_VISITOR_INTERFACE
#define QUIC_STREAM_FRAME_VISITOR_INTERFACE

#include <vector>
#include "quic/crypto/tls/type.h"
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

class IFrameVisitor {
public:
    IFrameVisitor() {}
    virtual ~IFrameVisitor() {}

    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) = 0;

    virtual std::shared_ptr<IBuffer> GetBuffer() = 0;

    virtual uint8_t GetEncryptionLevel() = 0;

    // flow contrel
    virtual void SetStreamDataSizeLimit(uint32_t size) = 0;
    virtual uint32_t GetLeftStreamDataSize() = 0;
    virtual void AddStreamDataSize(uint32_t size) {}
    virtual uint64_t GetStreamDataSize()  = 0;
};


}

#endif