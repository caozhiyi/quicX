#ifndef QUIC_FRAME_TYPE
#define QUIC_FRAME_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

enum FrameType: uint16_t {
    FT_PADDING                         = 0x00,
    FT_PING                            = 0x01,
    FT_ACK                             = 0x02,
    FT_ACK_ECN                         = 0x03,
    FT_RESET_STREAM                    = 0x04,
    FT_STOP_SENDING                    = 0x05,
    FT_CRYPTO                          = 0x06,
    FT_NEW_TOKEN                       = 0x07,
    FT_STREAM                          = 0x08,
    FT_MAX_DATA                        = 0x10,
    FT_MAX_STREAM_DATA                 = 0x11,
    FT_MAX_STREAMS_BIDIRECTIONAL       = 0x12,
    FT_MAX_STREAMS_UNIDIRECTIONAL      = 0x13,
    FT_DATA_BLOCKED                    = 0x14,
    FT_STREAM_DATA_BLOCKED             = 0x15,
    FT_STREAMS_BLOCKED_BIDIRECTIONAL   = 0x16,
    FT_STREAMS_BLOCKED_UNIDIRECTIONAL  = 0x17,
    FT_NEW_CONNECTION_ID               = 0x18,
    FT_RETIRE_CONNECTION_ID            = 0x19,
    FT_PATH_CHALLENGE                  = 0x1a,
    FT_PATH_RESPONSE                   = 0x1b,
    FT_CONNECTION_CLOSE                = 0x1c,
    FT_CONNECTION_CLOSE_APP            = 0x1d,
    FT_HANDSHAKE_DONE                  = 0x1e,

    FT_UNKNOW                          = 0xff,
};


enum FrameTypeBit: uint32_t {
    FTB_PADDING                         = 1 << FT_PADDING,
    FTB_PING                            = 1 << FT_PING,
    FTB_ACK                             = 1 << FT_ACK,
    FTB_ACK_ECN                         = 1 << FT_ACK_ECN,
    FTB_RESET_STREAM                    = 1 << FT_RESET_STREAM,
    FTB_STOP_SENDING                    = 1 << FT_STOP_SENDING,
    FTB_CRYPTO                          = 1 << FT_CRYPTO,
    FTB_NEW_TOKEN                       = 1 << FT_NEW_TOKEN,
    FTB_STREAM                          = 1 << FT_STREAM,
    FTB_MAX_DATA                        = 1 << FT_MAX_DATA,
    FTB_MAX_STREAM_DATA                 = 1 << FT_MAX_STREAM_DATA,
    FTB_MAX_STREAMS_BIDIRECTIONAL       = 1 << FT_MAX_STREAMS_BIDIRECTIONAL,
    FTB_MAX_STREAMS_UNIDIRECTIONAL      = 1 << FT_MAX_STREAMS_UNIDIRECTIONAL,
    FTB_DATA_BLOCKED                    = 1 << FT_DATA_BLOCKED,
    FTB_STREAM_DATA_BLOCKED             = 1 << FT_STREAM_DATA_BLOCKED,
    FTB_STREAMS_BLOCKED_BIDIRECTIONAL   = 1 << FT_STREAMS_BLOCKED_BIDIRECTIONAL,
    FTB_STREAMS_BLOCKED_UNIDIRECTIONAL  = 1 << FT_STREAMS_BLOCKED_UNIDIRECTIONAL,
    FTB_NEW_CONNECTION_ID               = 1 << FT_NEW_CONNECTION_ID,
    FTB_RETIRE_CONNECTION_ID            = 1 << FT_RETIRE_CONNECTION_ID,
    FTB_PATH_CHALLENGE                  = 1 << FT_PATH_CHALLENGE,
    FTB_PATH_RESPONSE                   = 1 << FT_PATH_RESPONSE,
    FTB_CONNECTION_CLOSE                = 1 << FT_CONNECTION_CLOSE,
    FTB_CONNECTION_CLOSE_APP            = 1 << FT_CONNECTION_CLOSE_APP,
    FTB_HANDSHAKE_DONE                  = 1 << FT_HANDSHAKE_DONE,
};

}
}

#endif