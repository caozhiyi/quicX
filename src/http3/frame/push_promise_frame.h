#ifndef HTTP3_FRAME_PUSH_PROMISE_FRAME
#define HTTP3_FRAME_PUSH_PROMISE_FRAME

#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class PushPromiseFrame:
    public IFrame {
public:
    PushPromiseFrame(): IFrame(FrameType::kPushPromise), length_(0), push_id_(0) {}

    uint64_t GetPushId() const { return push_id_; }
    void SetPushId(uint64_t id) { push_id_ = id; }

    const std::vector<uint8_t>& GetEncodedFields() const { return encoded_fields_; }
    void SetEncodedFields(const std::vector<uint8_t>& fields) { encoded_fields_ = fields; }

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();

private:
    uint64_t length_;
    uint64_t push_id_;
    std::vector<uint8_t> encoded_fields_;
};

}
}

#endif 