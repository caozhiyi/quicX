#ifndef HTTP3_FRAME_CANCEL_PUSH_FRAME
#define HTTP3_FRAME_CANCEL_PUSH_FRAME

#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class CancelPushFrame:
    public IFrame {
public:
    CancelPushFrame(): IFrame(FT_CANCEL_PUSH), push_id_(0) {}

    uint64_t GetPushId() const { return push_id_; }
    void SetPushId(uint64_t id) { push_id_ = id; }
    
    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();
private:
    uint64_t push_id_;
};

}
}

#endif 