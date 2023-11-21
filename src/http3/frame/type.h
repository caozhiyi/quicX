#ifndef HTTP3_FRAME_TYPE
#define HTTP3_FRAME_TYPE

#include <cstdint>

namespace quicx {
namespace http3 {

enum FrameType: uint16_t {
    FT_DATA          = 0x00,
    FT_HEADERS       = 0x01,
    FT_CANCEL_PUSH   = 0x03,
    FT_SETTINGS      = 0x04,
    FT_PUSH_PROMISE  = 0x05,
    FT_GOAWAY        = 0x07,
    FT_MAX_PUSH_ID   = 0x0d,

    FT_UNKNOW        = 0xff,
};

}
}

#endif