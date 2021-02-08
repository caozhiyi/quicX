#ifndef QUIC_FRAME_PATH_RESPONSE_FRAME
#define QUIC_FRAME_PATH_RESPONSE_FRAME

#include "frame_interface.h"

namespace quicx {

class PathResponseFrame : public Frame {
public:
    PathResponseFrame() : Frame(FT_PATH_RESPONSE) {}
    ~PathResponseFrame() {}

private:
    char* _data;  // 8-byte field contains arbitrary data.
};

}

#endif