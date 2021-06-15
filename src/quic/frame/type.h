#ifndef QUIC_FRAME_TYPE
#define QUIC_FRAME_TYPE

namespace quicx {

enum FrameType: uint32_t {
    FT_PADDING                    = 0x00,
    FT_PING                       = 0x01,
    FT_ACK                        = 0x02,
    FT_ACK_ECN                    = 0x03,
    FT_RESET_STREAM               = 0x04,
    FT_STOP_SENDING               = 0x05,
    FT_CRYPTO                     = 0x06,
    FT_NEW_TOKEN                  = 0x07,
    FT_STREAM                     = 0x08,
    FT_STREAM_MAX                 = 0x0f,
    FT_MAX_DATA                   = 0x10,
    FT_MAX_STREAM_DATA            = 0x11,
    FT_MAX_STREAMS_BIDIRECTIONAL  = 0x12,
    FT_MAX_STREAMS_UNIDIRECTIONAL = 0x13,
    FT_DATA_BLOCKED               = 0x14,
    FT_STREAM_DATA_BLOCKED        = 0x15,
    FT_STREAMS_BLOCKED_BIDIRECTIONAL   = 0x16,
    FT_STREAMS_BLOCKED_UNIDIRECTIONAL  = 0x17,
    FT_NEW_CONNECTION_ID          = 0x18,
    FT_RETIRE_CONNECTION_ID       = 0x19,
    FT_PATH_CHALLENGE             = 0x1a,
    FT_PATH_RESPONSE              = 0x1b,
    FT_CONNECTION_CLOSE           = 0x1c,
    FT_CONNECTION_CLOSE_APP       = 0x1d,
    FT_HANDSHAKE_DONE             = 0x1e,
};

}

#endif