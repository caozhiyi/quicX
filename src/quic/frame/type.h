#ifndef QUIC_FRAME_TYPE
#define QUIC_FRAME_TYPE

#include <cstdint>
#include "common/log/log.h"

namespace quicx {
namespace quic {

#define CHECK_ENCODE_ERROR(expr, error_msg) \
    do { \
        if (!(expr)) { \
            common::LOG_ERROR(error_msg); \
            return false; \
        } \
    } while (0);

#define CHECK_DECODE_ERROR(expr, error_msg) \
    do { \
        if (!(expr)) { \
            common::LOG_ERROR(error_msg); \
            return false; \
        } \
    } while (0);

enum FrameType: uint16_t {
    kPadding                         = 0x00,
    kPing                            = 0x01,
    kAck                             = 0x02,
    kAckEcn                          = 0x03,
    kResetStream                     = 0x04,
    kStopSending                     = 0x05,
    kCrypto                          = 0x06,
    kNewToken                        = 0x07,
    kStream                          = 0x08,
    kMaxData                         = 0x10,
    kMaxStreamData                   = 0x11,
    kMaxStreamsBidirectional         = 0x12,
    kMaxStreamsUnidirectional        = 0x13,
    kDataBlocked                     = 0x14,
    kStreamDataBlocked               = 0x15,
    kStreamsBlockedBidirectional     = 0x16,
    kStreamsBlockedUnidirectional    = 0x17,
    kNewConnectionId                 = 0x18,
    kRetireConnectionId              = 0x19,
    kPathChallenge                   = 0x1a,
    kPathResponse                    = 0x1b,
    kConnectionClose                 = 0x1c,
    kConnectionCloseApp              = 0x1d,
    kHandshakeDone                   = 0x1e,

    kUnknown                         = 0xff,
};


enum FrameTypeBit: uint32_t {
    kPaddingBit                         = 1 << FrameType::kPadding,
    kPingBit                            = 1 << FrameType::kPing,
    kAckBit                             = 1 << FrameType::kAck,
    kAckEcnBit                          = 1 << FrameType::kAckEcn,
    kResetStreamBit                     = 1 << FrameType::kResetStream,
    kStopSendingBit                     = 1 << FrameType::kStopSending,
    kCryptoBit                          = 1 << FrameType::kCrypto,
    kNewTokenBit                        = 1 << FrameType::kNewToken,
    kStreamBit                          = 1 << FrameType::kStream,
    kMaxDataBit                         = 1 << FrameType::kMaxData,
    kMaxStreamDataBit                   = 1 << FrameType::kMaxStreamData,
    kMaxStreamsBidirectionalBit         = 1 << FrameType::kMaxStreamsBidirectional,
    kMaxStreamsUnidirectionalBit        = 1 << FrameType::kMaxStreamsUnidirectional,
    kDataBlockedBit                     = 1 << FrameType::kDataBlocked,
    kStreamDataBlockedBit               = 1 << FrameType::kStreamDataBlocked,
    kStreamsBlockedBidirectionalBit     = 1 << FrameType::kStreamsBlockedBidirectional,
    kStreamsBlockedUnidirectionalBit    = 1 << FrameType::kStreamsBlockedUnidirectional,
    kNewConnectionIdBit                 = 1 << FrameType::kNewConnectionId,
    kRetireConnectionIdBit              = 1 << FrameType::kRetireConnectionId,
    kPathChallengeBit                   = 1 << FrameType::kPathChallenge,
    kPathResponseBit                    = 1 << FrameType::kPathResponse,
    kConnectionCloseBit                 = 1 << FrameType::kConnectionClose,
    kConnectionCloseAppBit              = 1 << FrameType::kConnectionCloseApp,
    kHandshakeDoneBit                   = 1 << FrameType::kHandshakeDone,
};

}
}

#endif