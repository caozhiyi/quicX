#ifndef QUIC_FRAME_PATH_CHALLENGE_FRAME
#define QUIC_FRAME_PATH_CHALLENGE_FRAME

#include "frame_interface.h"

namespace quicx {

class PathChallengeFrame : public Frame {
public:
    PathChallengeFrame() : Frame(FT_PATH_CHALLENGE) {}
    ~PathChallengeFrame() {}

private:
    char* _data;  // 8-byte field contains arbitrary data.
};

}

#endif