#include <cstdlib> // for abort
#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/connection/util.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace quic {

bool IsAckElictingPacket(uint32_t frame_type) {
    return ((frame_type) & ~(FrameTypeBit::kAckBit | FrameTypeBit::kAckEcnBit | FrameTypeBit::kPaddingBit | FrameTypeBit::kConnectionCloseBit));
}

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level) {
    switch (level) {
    case PCL_INITIAL: return PNS_INITIAL;
    case PCL_HANDSHAKE:  return PNS_HANDSHAKE;
    case PCL_ELAY_DATA:
    case PCL_APPLICATION: return PNS_APPLICATION;
    default:
        abort(); // TODO
    }
}

const std::string FrameType2String(uint16_t frame_type) {
    switch (frame_type) {
        case FrameType::kPadding:                      return "PADDING";
        case FrameType::kPing:                         return "PING";
        case FrameType::kAck:                          return "ACK";
        case FrameType::kAckEcn:                       return "ACK_ECN";
        case FrameType::kCrypto:                       return "CRYPTO";
        case FrameType::kNewToken:                     return "NEW_TOKEN";
        case FrameType::kMaxData:                      return "MAX_DATA";
        case FrameType::kMaxStreamsBidirectional:      return "MAX_STREAMS_BIDIRECTIONAL";
        case FrameType::kMaxStreamsUnidirectional:     return "MAX_STREAMS_UNIDIRECTIONAL";
        case FrameType::kDataBlocked:                  return "DATA_BLOCKED";
        case FrameType::kStreamsBlockedBidirectional:  return "STREAMS_BLOCKED_BIDIRECTIONAL";
        case FrameType::kStreamsBlockedUnidirectional: return "STREAMS_BLOCKED_UNIDIRECTIONAL";
        case FrameType::kNewConnectionId:              return "NEW_CONNECTION_ID";
        case FrameType::kRetireConnectionId:           return "RETIRE_CONNECTION_ID";
        case FrameType::kPathChallenge:                return "PATH_CHALLENGE";
        case FrameType::kPathResponse:                 return "PATH_RESPONSE";
        case FrameType::kConnectionClose:              return "CONNECTION_CLOSE";
        case FrameType::kConnectionCloseApp:           return "CONNECTION_CLOSE_APP";
        case FrameType::kHandshakeDone:                return "HANDSHAKE_DONE";
        case FrameType::kResetStream:                  return "RESET_STREAM";
        case FrameType::kStopSending:                  return "STOP_SENDING";
        case FrameType::kStreamDataBlocked:            return "STREAM_DATA_BLOCKED";
        case FrameType::kMaxStreamData:                return "MAX_STREAM_DATA";
        default:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                return "STREAM_DATA";
            } else {
                common::LOG_ERROR("invalid frame type. type:%s", frame_type);
            }
    }
    return "UNKNOWN";
}

}
}
