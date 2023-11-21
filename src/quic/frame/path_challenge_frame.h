#ifndef QUIC_FRAME_PATH_CHALLENGE_FRAME
#define QUIC_FRAME_PATH_CHALLENGE_FRAME

#include "common/util/random.h"
#include "quic/frame/frame_interface.h"

namespace quicx {
namespace quic {

static const uint16_t __path_data_length = 8;

class PathResponseFrame;
class PathChallengeFrame:
    public IFrame {
public:
    PathChallengeFrame();
    ~PathChallengeFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    bool CompareData(std::shared_ptr<PathResponseFrame> response);

    void MakeData();
    uint8_t* GetData() { return _data; }

private:
    uint8_t _data[__path_data_length];  // 8-byte field contains arbitrary data.
    static std::shared_ptr<common::RangeRandom> _random;
};

}
}

#endif