// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_UTIL_QLOG_TYPES
#define COMMON_QLOG_UTIL_QLOG_TYPES

#include "common/qlog/qlog_config.h"
#include "quic/frame/type.h"
#include "quic/packet/type.h"

namespace quicx {
namespace common {

/**
 * @brief Convert quic::PacketType to qlog string
 */
inline const char* PacketTypeToQlogString(quic::PacketType type) {
    switch (type) {
        case quic::PacketType::kInitialPacketType:
            return "initial";
        case quic::PacketType::k0RttPacketType:
            return "0RTT";
        case quic::PacketType::kHandshakePacketType:
            return "handshake";
        case quic::PacketType::kRetryPacketType:
            return "retry";
        case quic::PacketType::kNegotiationPacketType:
            return "version_negotiation";
        case quic::PacketType::k1RttPacketType:
            return "1RTT";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert quic::FrameType to qlog string
 */
inline const char* FrameTypeToQlogString(quic::FrameType type) {
    switch (type) {
        case quic::FrameType::kPadding:
            return "padding";
        case quic::FrameType::kPing:
            return "ping";
        case quic::FrameType::kAck:
            return "ack";
        case quic::FrameType::kAckEcn:
            return "ack_ecn";
        case quic::FrameType::kResetStream:
            return "reset_stream";
        case quic::FrameType::kStopSending:
            return "stop_sending";
        case quic::FrameType::kCrypto:
            return "crypto";
        case quic::FrameType::kNewToken:
            return "new_token";
        case quic::FrameType::kStream:
            return "stream";
        case quic::FrameType::kMaxData:
            return "max_data";
        case quic::FrameType::kMaxStreamData:
            return "max_stream_data";
        case quic::FrameType::kMaxStreamsBidirectional:
            return "max_streams_bidi";
        case quic::FrameType::kMaxStreamsUnidirectional:
            return "max_streams_uni";
        case quic::FrameType::kDataBlocked:
            return "data_blocked";
        case quic::FrameType::kStreamDataBlocked:
            return "stream_data_blocked";
        case quic::FrameType::kStreamsBlockedBidirectional:
            return "streams_blocked_bidi";
        case quic::FrameType::kStreamsBlockedUnidirectional:
            return "streams_blocked_uni";
        case quic::FrameType::kNewConnectionId:
            return "new_connection_id";
        case quic::FrameType::kRetireConnectionId:
            return "retire_connection_id";
        case quic::FrameType::kPathChallenge:
            return "path_challenge";
        case quic::FrameType::kPathResponse:
            return "path_response";
        case quic::FrameType::kConnectionClose:
            return "connection_close";
        case quic::FrameType::kConnectionCloseApp:
            return "connection_close_app";
        case quic::FrameType::kHandshakeDone:
            return "handshake_done";
        default:
            return "unknown";
    }
}

/**
 * @brief Convert VantagePoint to string
 */
inline const char* VantagePointToString(VantagePoint vp) {
    switch (vp) {
        case VantagePoint::kClient:
            return "client";
        case VantagePoint::kServer:
            return "server";
        case VantagePoint::kNetwork:
            return "network";
        default:
            return "unknown";
    }
}

}  // namespace common
}  // namespace quicx

#endif
