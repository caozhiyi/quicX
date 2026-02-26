#ifndef QUIC_FRAME_STREAM_FRAME_INTERFACE
#define QUIC_FRAME_STREAM_FRAME_INTERFACE

#include "quic/frame/type.h"
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

/**
 * @brief Base interface for stream-related QUIC frames
 *
 * Extends IFrame with stream ID management.
 */
class IStreamFrame: public IFrame {
public:
    IStreamFrame(uint16_t ft = FrameType::kUnknown): IFrame(ft), stream_id_(0) {}
    virtual ~IStreamFrame() {}

    /**
     * @brief Set the stream ID
     *
     * @param id Stream identifier
     */
    void SetStreamID(uint64_t id) { stream_id_ = id; }

    /**
     * @brief Get the stream ID
     *
     * @return Stream identifier
     */
    uint64_t GetStreamID() { return stream_id_; }

protected:
    uint64_t stream_id_;
};

}
}

#endif