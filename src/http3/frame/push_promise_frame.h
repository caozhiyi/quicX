#ifndef HTTP3_FRAME_PUSH_PROMISE_FRAME
#define HTTP3_FRAME_PUSH_PROMISE_FRAME

#include <cstdint>
#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class PushPromiseFrame: public IFrame {
public:
    PushPromiseFrame():
        IFrame(FrameType::kPushPromise),
        length_(0),
        push_id_(0) {}

    uint64_t GetPushId() const { return push_id_; }
    void SetPushId(uint64_t id) { push_id_ = id; }

    const std::shared_ptr<common::IBuffer> GetEncodedFields() const { return encoded_fields_; }
    void SetEncodedFields(const std::shared_ptr<common::IBuffer> fields) { encoded_fields_ = fields; }

    bool Encode(std::shared_ptr<common::IBuffer> buffer) override;
    DecodeResult Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false) override;
    uint32_t EvaluateEncodeSize() override;
    uint32_t EvaluatePayloadSize() override;

private:
    uint64_t length_;
    uint64_t push_id_;
    std::shared_ptr<common::IBuffer> encoded_fields_;
};

}  // namespace http3
}  // namespace quicx

#endif