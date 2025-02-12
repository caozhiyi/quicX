#ifndef QUIC_FRAME_STREAM_FRAME_INTERFACE
#define QUIC_FRAME_STREAM_FRAME_INTERFACE

#include <memory>
#include "quic/frame/type.h"
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class IStreamFrame:
    public IFrame {
public:
    IStreamFrame(uint16_t ft = FrameType::kUnknown): IFrame(ft), stream_id_(0) {}
    virtual ~IStreamFrame() {}

    void SetStreamID(uint64_t id) { stream_id_ = id; }
    uint64_t GetStreamID() { return stream_id_; }

protected:
    uint64_t stream_id_;     // indicating the stream ID of the stream.
};

}
}

#endif