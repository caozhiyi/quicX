#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>

namespace quicx {

class Frame;
class Buffer;
class AlloterWrap;
bool DecodeFrame(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, std::vector<std::shared_ptr<Frame>>& frames);

}

#endif