#ifndef QUIC_HTTP3_FRAME_DECODE
#define QUIC_HTTP3_FRAME_DECODE

#include <memory>
#include <vector>
#include "http3/frame/if_frame.h"

namespace quicx {
namespace http3 {

bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames);

}
}

#endif