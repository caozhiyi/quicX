#ifndef QUIC_STREAM_IF_FRAME_VISITOR
#define QUIC_STREAM_IF_FRAME_VISITOR

#include <vector>
#include "quic/frame/if_frame.h"
#include "common/buffer/if_buffer.h"
#include "quic/connection/controler/send_control.h"

namespace quicx {
namespace quic {

// Error type for frame encoding
enum class FrameEncodeError {
    kNone = 0,              // No error
    kInsufficientSpace = 1, // Insufficient buffer space
    kOtherError = 2,        // Other encoding errors
};

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

    // Remaining writable bytes in the visitor's underlying packet buffer.
    // Used by stream/crypto producers to size each frame's payload so the
    // STREAM/CRYPTO frame *always* fits in the current datagram. Default
    // returns UINT32_MAX (i.e. "no packet-level cap"); FixBufferFrameVisitor
    // overrides this to expose buffer_->GetFreeLength().
    virtual uint32_t GetPacketLeftSize() { return UINT32_MAX; }

    // Get stream data info for ACK tracking
    virtual std::vector<StreamDataInfo> GetStreamDataInfo() const { return {}; }

    // Get last encoding error
    virtual FrameEncodeError GetLastError() const { return FrameEncodeError::kNone; }
};


}
}

#endif