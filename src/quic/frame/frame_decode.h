#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>
#include "quic/frame/if_frame.h"
#include "common/buffer/if_buffer_read.h"

namespace quicx {
namespace quic {

bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

}
}

#endif