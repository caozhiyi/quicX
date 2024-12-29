#ifndef HTTP3_FRAME_HEADERS_FRAME
#define HTTP3_FRAME_HEADERS_FRAME

#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace http3 {

class HeadersFrame:
    public IFrame {
public:
    HeadersFrame(): IFrame(FT_HEADERS), length_(0) {}

    uint32_t GetLength() const { return length_; }
    void SetLength(uint32_t length) { length_ = length; }

    std::shared_ptr<common::IBufferRead> GetEncodedFields() const { return encoded_fields_; }
    void SetEncodedFields(std::shared_ptr<common::IBufferRead> fields) { encoded_fields_ = fields; }

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();

private:
    uint32_t length_; // Length of the frame
    std::shared_ptr<common::IBufferRead> encoded_fields_;
};

}
}

#endif


