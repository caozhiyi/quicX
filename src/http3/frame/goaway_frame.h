#ifndef HTTP3_FRAME_GOAWAY_FRAME
#define HTTP3_FRAME_GOAWAY_FRAME

#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class GoawayFrame:
    public IFrame {
public:
    GoawayFrame(): IFrame(FT_GOAWAY), stream_id_(0) {}

    uint64_t GetStreamId() const { return stream_id_; }
    void SetStreamId(uint64_t id) { stream_id_ = id; }

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();

private:
    uint64_t stream_id_;
};

}
}

#endif 