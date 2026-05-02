#ifndef HTTP3_FRAME_FRAME_DECODER
#define HTTP3_FRAME_FRAME_DECODER

#include <memory>
#include <cstdint>

#include "common/buffer/if_buffer.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace common {
class QlogTrace;
}
namespace http3 {

// FrameDecoder maintains state for incremental frame decoding.
// It handles partial frames by preserving the current frame being decoded
// across multiple calls to DecodeFrames.
class FrameDecoder {
public:
    FrameDecoder();
    ~FrameDecoder();

    // Decode frames from buffer. Returns false on error, true otherwise.
    // Decoded frames are appended to the frames vector.
    // If a frame is partially decoded (kNeedMoreData), it's saved internally
    // and will be resumed on the next call.
    bool DecodeFrames(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

    // Set qlog trace for frame_parsed events
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) { qlog_trace_ = trace; }

    // Set stream ID for qlog context
    void SetStreamId(uint64_t stream_id) { stream_id_ = stream_id; }

private:
    enum class State {
        kReadingFrameType,      // Waiting to read frame type
        kDecodingFrame,         // Currently decoding a frame's payload
        kSkippingUnknownFrame   // Skipping payload of an unknown frame type (RFC 9114 §9)
    };

    State state_;
    std::shared_ptr<IFrame> current_frame_;  // Frame being decoded
    uint64_t current_frame_type_;            // Type of current frame
    uint64_t skip_remaining_ = 0;            // Bytes remaining to skip for unknown frame

    // Qlog support
    std::shared_ptr<common::QlogTrace> qlog_trace_;
    uint64_t stream_id_ = 0;
};

}  // namespace http3
}  // namespace quicx

#endif
