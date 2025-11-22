#ifndef HTTP3_FRAME_DATA_FRAME
#define HTTP3_FRAME_DATA_FRAME

#include <memory>
#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace http3 {

class DataFrame: public IFrame {
public:
    DataFrame():
        IFrame(FrameType::kData),
        length_(0) {}

    uint32_t GetLength() const { return length_; }
    void SetLength(uint32_t length) { length_ = length; }

    std::shared_ptr<common::IBuffer> GetData() const { return data_; }
    void SetData(std::shared_ptr<common::IBuffer> data) { data_ = data; }

    bool Encode(std::shared_ptr<common::IBuffer> buffer) override;
    DecodeResult Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false) override;
    uint32_t EvaluateEncodeSize() override;
    uint32_t EvaluatePayloadSize() override;

private:
    uint64_t length_;
    std::shared_ptr<common::IBuffer> data_;
};

}  // namespace http3
}  // namespace quicx

#endif
