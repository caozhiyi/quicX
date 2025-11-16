#ifndef HTTP3_FRAME_HEADERS_FRAME
#define HTTP3_FRAME_HEADERS_FRAME

#include <cstdint>
#include "http3/frame/type.h"
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

class HeadersFrame:
    public IFrame {
public:
    HeadersFrame(): IFrame(FrameType::kHeaders), length_(0) {}

    uint32_t GetLength() const { return length_; }
    void SetLength(uint32_t length) { length_ = length; }

    std::shared_ptr<common::IBuffer> GetEncodedFields() const { return encoded_fields_; }
    void SetEncodedFields(std::shared_ptr<common::IBuffer> fields) { encoded_fields_ = fields; }

    bool Encode(std::shared_ptr<common::IBuffer> buffer);
    bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    uint32_t EvaluateEncodeSize();
    uint32_t EvaluatePayloadSize();

private:
    uint32_t length_; // Length of the frame
    std::shared_ptr<common::IBuffer> encoded_fields_; // Encoded fields
};

}
}

#endif


