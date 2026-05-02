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


// Note: kUnknown (0xff) MUST NOT be used with bit-shift operations as it exceeds uint32_t width.
// Use GetFrameTypeBit() which includes bounds checking for safe conversion.
enum FrameTypeBit: uint32_t {
    kPaddingBit                         = 1u << FrameType::kPadding,
    kPingBit                            = 1u << FrameType::kPing,
    kAckBit                             = 1u << FrameType::kAck,
    kAckEcnBit                          = 1u << FrameType::kAckEcn,
    kResetStreamBit                     = 1u << FrameType::kResetStream,
    kStopSendingBit                     = 1u << FrameType::kStopSending,
    kCryptoBit                          = 1u << FrameType::kCrypto,
    kNewTokenBit                        = 1u << FrameType::kNewToken,
    kStreamBit                          = 1u << FrameType::kStream,
    kMaxDataBit                         = 1u << FrameType::kMaxData,
    kMaxStreamDataBit                   = 1u << FrameType::kMaxStreamData,
    kMaxStreamsBidirectionalBit         = 1u << FrameType::kMaxStreamsBidirectional,
    kMaxStreamsUnidirectionalBit        = 1u << FrameType::kMaxStreamsUnidirectional,
    kDataBlockedBit                     = 1u << FrameType::kDataBlocked,
    kStreamDataBlockedBit               = 1u << FrameType::kStreamDataBlocked,
    kStreamsBlockedBidirectionalBit     = 1u << FrameType::kStreamsBlockedBidirectional,
    kStreamsBlockedUnidirectionalBit    = 1u << FrameType::kStreamsBlockedUnidirectional,
    kNewConnectionIdBit                 = 1u << FrameType::kNewConnectionId,
    kRetireConnectionIdBit              = 1u << FrameType::kRetireConnectionId,
    kPathChallengeBit                   = 1u << FrameType::kPathChallenge,
    kPathResponseBit                    = 1u << FrameType::kPathResponse,
    kConnectionCloseBit                 = 1u << FrameType::kConnectionClose,
    kConnectionCloseAppBit              = 1u << FrameType::kConnectionCloseApp,
    kHandshakeDoneBit                   = 1u << FrameType::kHandshakeDone,
};

}
}

#endif