#ifndef HTTP3_FRAME_DATA_FRAME
#define HTTP3_FRAME_DATA_FRAME

#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class DataFrame:
    public IFrame {
public:
    DataFrame(): IFrame(FrameType::kData), length_(0) {}

    uint32_t GetLength() const { return length_; }
    void SetLength(uint32_t length) { length_ = length; }

    const std::vector<uint8_t>& GetData() const { return data_; }
    void SetData(const std::vector<uint8_t>& data) { data_ = data; }

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();

private:
    uint64_t length_;
    std::vector<uint8_t> data_;
};

}
}

#endif
