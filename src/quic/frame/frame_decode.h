#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>

namespace quicx {

class IFrame;
class IBufferReadOnly;
bool DecodeFrames(std::shared_ptr<IBufferReadOnly> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

}

#endif