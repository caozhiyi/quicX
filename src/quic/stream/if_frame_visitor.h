#ifndef QUIC_STREAM_IF_FRAME_VISITOR
#define QUIC_STREAM_IF_FRAME_VISITOR

#include <vector>
#include "quic/crypto/tls/type.h"
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {
namespace quic {

/*
 iterate all frames in connection, try to decode all data that can be sent
*/
class IFrameVisitor {
public:
    IFrameVisitor() {}
    virtual ~IFrameVisitor() {}

    // try to decode frame data, return true if decode success
    virtual bool HandleFrame(std::shared_ptr<IFrame> frame) = 0;

    // return buffer that contains all data that can be sent
    virtual std::shared_ptr<common::IBuffer> GetBuffer() = 0;

    virtual uint8_t GetEncryptionLevel() = 0;

    // flow contrel
    virtual void SetStreamDataSizeLimit(uint32_t size) = 0;
    virtual uint32_t GetLeftStreamDataSize() = 0;
    virtual void AddStreamDataSize(uint32_t size) {}
    virtual uint64_t GetStreamDataSize()  = 0;
};


}
}

#endif