#ifndef QUIC_FRAME_PATH_CHALLENGE_FRAME
#define QUIC_FRAME_PATH_CHALLENGE_FRAME

#include "frame_interface.h"

namespace quicx {

static const uint16_t __path_data_length = 8;

class RangeRandom;
class PathResponseFrame;
class PathChallengeFrame: public Frame {
public:
    PathChallengeFrame();
    ~PathChallengeFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    bool CompareData(std::shared_ptr<PathResponseFrame> response);

    void MakeData();
    char* GetData() { return _data; }

private:
    char _data[__path_data_length];  // 8-byte field contains arbitrary data.
    static std::shared_ptr<RangeRandom> _random;
};

}

#endif