#ifndef QUIC_FRAME_PATH_RESPONSE_FRAME
#define QUIC_FRAME_PATH_RESPONSE_FRAME

#include "path_challenge_frame.h"

namespace quicx {

class PathResponseFrame: public Frame {
public:
    PathResponseFrame();
    ~PathResponseFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetData(char* data);
    char* GetData() { return _data; }

private:
    char _data[__path_data_length];  // 8-byte field contains arbitrary data.
};

}

#endif