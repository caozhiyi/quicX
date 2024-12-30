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

    const std::vector<uint8_t>& GetEncodedFields() const { return encoded_fields_; }
    void SetEncodedFields(const std::vector<uint8_t>& fields) { encoded_fields_ = fields; }

    bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePaloadSize();

private:
    uint32_t length_; // Length of the frame
    std::vector<uint8_t> encoded_fields_; // Encoded fields
};

}
}

#endif


