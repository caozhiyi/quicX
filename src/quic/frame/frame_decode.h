#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>
#include "quic/frame/frame_interface.h"
#include "common/buffer/buffer_read_interface.h"

namespace quicx {

bool DecodeFrames(std::shared_ptr<IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

}

#endif