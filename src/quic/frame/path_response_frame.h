#ifndef QUIC_FRAME_PATH_RESPONSE_FRAME
#define QUIC_FRAME_PATH_RESPONSE_FRAME

#include "quic/frame/path_challenge_frame.h"

namespace quicx {
namespace quic {

class PathResponseFrame:
    public IFrame {
public:
    PathResponseFrame();
    ~PathResponseFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetData(uint8_t* data);
    uint8_t* GetData() { return data_; }

private:
    uint8_t data_[__path_data_length];  // 8-byte field contains arbitrary data.
};

}
}

#endif