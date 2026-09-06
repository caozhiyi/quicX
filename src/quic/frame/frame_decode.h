#ifndef QUIC_FRAME_FRAME_DECODE
#define QUIC_FRAME_FRAME_DECODE

#include <memory>
#include <vector>

#include "common/buffer/if_buffer.h"

#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

bool DecodeFrames(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

}
}  // namespace quicx

#endif  // QUIC_FRAME_FRAME_DECODE