#ifndef HTTP3_FRAME_TYPE
#define HTTP3_FRAME_TYPE

#include <cstdint>

namespace quicx {
namespace http3 {

enum FrameType: uint16_t {
    kData            = 0x00,
    kHeaders         = 0x01,
    kCancelPush      = 0x03,
    kSettings        = 0x04,
    kPushPromise     = 0x05,
    kGoAway          = 0x07,
    kMaxPushId       = 0x0d,

    kUnknown         = 0xff,
};

}
}

#endif