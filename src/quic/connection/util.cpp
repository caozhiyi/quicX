#include <cstdlib> // for abort
#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/connection/util.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace quic {

bool IsAckElictingPacket(uint32_t frame_type) {
    return ((frame_type) & ~(FTB_ACK | FTB_ACK_ECN | FTB_PADDING | FTB_CONNECTION_CLOSE));
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
        case FT_PADDING:                        return "PADDING";
        case FT_PING:                           return "PING";
        case FT_ACK:                            return "ACK";
        case FT_ACK_ECN:                        return "ACK_ECN";
        case FT_CRYPTO:                         return "CRYPTO";
        case FT_NEW_TOKEN:                      return "NEW_TOKEN";
        case FT_MAX_DATA:                       return "MAX_DATA";
        case FT_MAX_STREAMS_BIDIRECTIONAL:      return "MAX_STREAMS_BIDIRECTIONAL";
        case FT_MAX_STREAMS_UNIDIRECTIONAL:     return "MAX_STREAMS_UNIDIRECTIONAL";
        case FT_DATA_BLOCKED:                   return "DATA_BLOCKED";
        case FT_STREAMS_BLOCKED_BIDIRECTIONAL:  return "STREAMS_BLOCKED_BIDIRECTIONAL";
        case FT_STREAMS_BLOCKED_UNIDIRECTIONAL: return "STREAMS_BLOCKED_UNIDIRECTIONAL";
        case FT_NEW_CONNECTION_ID:              return "NEW_CONNECTION_ID";
        case FT_RETIRE_CONNECTION_ID:           return "RETIRE_CONNECTION_ID";
        case FT_PATH_CHALLENGE:                 return "PATH_CHALLENGE";
        case FT_PATH_RESPONSE:                  return "PATH_RESPONSE";
        case FT_CONNECTION_CLOSE:               return "CONNECTION_CLOSE";
        case FT_CONNECTION_CLOSE_APP:           return "CONNECTION_CLOSE_APP";
        case FT_HANDSHAKE_DONE:                 return "HANDSHAKE_DONE";
        case FT_RESET_STREAM:                   return "RESET_STREAM";
        case FT_STOP_SENDING:                   return "STOP_SENDING";
        case FT_STREAM_DATA_BLOCKED:            return "STREAM_DATA_BLOCKED";
        case FT_MAX_STREAM_DATA:                return "MAX_STREAM_DATA";
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
