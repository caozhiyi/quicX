#ifndef QUIC_FRAME_TYPE
#define QUIC_FRAME_TYPE

namespace quicx {

enum IetfFrameType: uint32_t {
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
    FT_STREAMS_BLOCKED__UNIDIRECTIONAL = 0x17,
    FT_NEW_CONNECTION_ID          = 0x18,
    FT_RETIRE_CONNECTION_ID       = 0x19,
    FT_PATH_CHALLENGE             = 0x1a,
    FT_PATH_RESPONSE              = 0x1b,
    FT_CONNECTION_CLOSE           = 0x1c,
    FT_CONNECTION_CLOSE_APP       = 0x1d,
    FT_HANDSHAKE_DONE             = 0x1e,
};

enum GoogleFrameType: uint32_t {
    // Regular frame types. The values set here cannot change without the
    // introduction of a new QUIC version.
    PADDING_FRAME = 0,
    RST_STREAM_FRAME = 1,
    CONNECTION_CLOSE_FRAME = 2,
    GOAWAY_FRAME = 3,
    WINDOW_UPDATE_FRAME = 4,
    BLOCKED_FRAME = 5,
    STOP_WAITING_FRAME = 6,
    PING_FRAME = 7,
    CRYPTO_FRAME = 8,
    // TODO(b/157935330): stop hard coding this when deprecate T050.
    HANDSHAKE_DONE_FRAME = 9,
    
    // STREAM and ACK frames are special frames. They are encoded differently on
    // the wire and their values do not need to be stable.
    STREAM_FRAME,
    ACK_FRAME,
    // The path MTU discovery frame is encoded as a PING frame on the wire.
    MTU_DISCOVERY_FRAME,
    
    // These are for IETF-specific frames for which there is no mapping
    // from Google QUIC frames. These are valid/allowed if and only if IETF-
    // QUIC has been negotiated. Values are not important, they are not
    // the values that are in the packets (see QuicIetfFrameType, below).
    NEW_CONNECTION_ID_FRAME,
    MAX_STREAMS_FRAME,
    STREAMS_BLOCKED_FRAME,
    PATH_RESPONSE_FRAME,
    PATH_CHALLENGE_FRAME,
    STOP_SENDING_FRAME,
    MESSAGE_FRAME,
    NEW_TOKEN_FRAME,
    RETIRE_CONNECTION_ID_FRAME,
    ACK_FREQUENCY_FRAME,
    
    NUM_FRAME_TYPES
};

}

#endif