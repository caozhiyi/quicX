#ifndef QUIC_FRAME_PADDING_FRAME
#define QUIC_FRAME_PADDING_FRAME

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class PaddingFrame:
    public IFrame {
public:
    PaddingFrame();
    ~PaddingFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetPaddingLength(uint32_t length) { padding_length_ = length; }
    uint32_t GetPaddingLength() { return padding_length_; }

private:
    uint32_t padding_length_;
};

}
}

#endif