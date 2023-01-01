#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>

namespace quicx {

class IFrame;
class IBufferRead;
bool DecodeFrames(std::shared_ptr<IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

}

#endif