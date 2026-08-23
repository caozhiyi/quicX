#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "quic/connection/util.h"
#include "common/log/log.h"
#include "quic/connection/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

bool ValidatePreferredAddressBinary(const std::string& raw) {
    // RFC 9000 §18.2 preferred_address value layout (all multi-byte fields
    // big-endian):
    //   IPv4 Address (32) | IPv4 Port (16) | IPv6 Address (128) | IPv6 Port (16) |
    //   Connection ID Length (8) | Connection ID (0..20) | Stateless Reset Token (128)
    static constexpr size_t kHeader = 4 + 2 + 16 + 2 + 1;  // 25 (up to, not including, the CID)
    static constexpr size_t kMinLen = kHeader + 16;        // CID length 0 + 16-byte reset token
    if (raw.size() < kMinLen) {
        return false;
    }
    const uint8_t cid_len = reinterpret_cast<const uint8_t*>(raw.data())[24];
    return cid_len <= kMaxCidLength && raw.size() >= kHeader + cid_len + 16;
}

bool ParsePreferredAddressBinary(const std::string& raw, bool peer_is_ipv4,
                                 common::Address& out, std::string* out_cid) {
    // Nothing may be read from |raw| before its layout is validated.
    if (!ValidatePreferredAddressBinary(raw)) {
        return false;
    }
    const size_t kHeader = 4 + 2 + 16 + 2 + 1;  // 25 (up to, not including, the CID)
    const uint8_t* p = reinterpret_cast<const uint8_t*>(raw.data());

    const uint32_t ipv4 = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                          (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    const uint16_t ipv4_port = (uint16_t(p[4]) << 8) | uint16_t(p[5]);
    const uint8_t* ipv6 = p + 6;
    const uint16_t ipv6_port = (uint16_t(p[22]) << 8) | uint16_t(p[23]);
    const uint8_t cid_len = p[24];

    // Prefer the address family that matches the current connection; a compliant
    // server fills both fields, so pick the one the client can actually reach.
    if (peer_is_ipv4 && ipv4 != 0) {
        char ip[INET_ADDRSTRLEN];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
        out = common::Address(ip, ipv4_port);
    } else {
        char ip[INET6_ADDRSTRLEN];
        struct in6_addr a;
        memcpy(&a, ipv6, 16);
        if (inet_ntop(AF_INET6, &a, ip, sizeof(ip)) == nullptr) {
            return false;
        }
        out = common::Address(ip, ipv6_port);
    }

    // RFC 9000 §9.6: the CID carried here is the one the client MUST use as the
    // new DCID on the migrated path.
    if (out_cid != nullptr && cid_len > 0) {
        *out_cid = std::string(reinterpret_cast<const char*>(p + kHeader), cid_len);
    }
    return true;
}

bool ParsePreferredAddress(const std::string& value, common::Address& out) {
    if (value.empty()) {
        return false;
    }

    std::string host;
    std::string port_str;

    if (value[0] == '[') {
        // Bracketed IPv6: "[<ipv6>]:<port>".
        const auto close = value.find(']');
        if (close == std::string::npos || close == 1) {
            return false;  // no closing bracket, or empty host
        }
        if (close + 1 >= value.size() || value[close + 1] != ':') {
            return false;  // must be followed by ":<port>"
        }
        host = value.substr(1, close - 1);
        port_str = value.substr(close + 2);

    } else {
        const auto colon = value.find(':');
        if (colon == std::string::npos || colon == 0) {
            return false;  // no port, or empty host
        }
        // More than one colon without brackets is an unbracketed IPv6 literal:
        // genuinely ambiguous, so refuse instead of guessing.
        if (value.find(':', colon + 1) != std::string::npos) {
            return false;
        }
        host = value.substr(0, colon);
        port_str = value.substr(colon + 1);
    }

    if (host.empty() || port_str.empty()) {
        return false;
    }

    // Hand-rolled rather than std::stoi/strtoul: no exceptions, no errno, and
    // trailing garbage ("443x") is rejected instead of silently ignored.
    uint32_t port = 0;
    for (char c : port_str) {
        if (c < '0' || c > '9') {
            return false;
        }
        port = port * 10 + static_cast<uint32_t>(c - '0');
        if (port > 65535) {
            return false;  // bail on overflow; also catches absurdly long input
        }
    }
    if (port == 0) {
        return false;  // port 0 is not a usable destination
    }

    out = common::Address(host, static_cast<uint16_t>(port));
    return true;
}

bool IsAckElictingPacket(uint32_t frame_type) {
    return ((frame_type) & ~(FrameTypeBit::kAckBit | FrameTypeBit::kAckEcnBit | FrameTypeBit::kPaddingBit |
                               FrameTypeBit::kConnectionCloseBit));
}

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level) {
    switch (level) {
        case PacketCryptoLevel::kInitialCryptoLevel:
            return PacketNumberSpace::kInitialNumberSpace;
        case PacketCryptoLevel::kHandshakeCryptoLevel:
            return PacketNumberSpace::kHandshakeNumberSpace;
        case PacketCryptoLevel::kEarlyDataCryptoLevel:
        case PacketCryptoLevel::kApplicationCryptoLevel:
            return PacketNumberSpace::kApplicationNumberSpace;
        default:
            LOG_ERROR("unknown crypto level: %d", level);
            return PacketNumberSpace::kInitialNumberSpace;  // safe fallback
    }
}

const std::string FrameType2String(uint16_t frame_type) {
    switch (frame_type) {
        case FrameType::kPadding:
            return "PADDING";
        case FrameType::kPing:
            return "PING";
        case FrameType::kAck:
            return "ACK";
        case FrameType::kAckEcn:
            return "ACK_ECN";
        case FrameType::kCrypto:
            return "CRYPTO";
        case FrameType::kNewToken:
            return "NEW_TOKEN";
        case FrameType::kMaxData:
            return "MAX_DATA";
        case FrameType::kMaxStreamsBidirectional:
            return "MAX_STREAMS_BIDIRECTIONAL";
        case FrameType::kMaxStreamsUnidirectional:
            return "MAX_STREAMS_UNIDIRECTIONAL";
        case FrameType::kDataBlocked:
            return "DATA_BLOCKED";
        case FrameType::kStreamsBlockedBidirectional:
            return "STREAMS_BLOCKED_BIDIRECTIONAL";
        case FrameType::kStreamsBlockedUnidirectional:
            return "STREAMS_BLOCKED_UNIDIRECTIONAL";
        case FrameType::kNewConnectionId:
            return "NEW_CONNECTION_ID";
        case FrameType::kRetireConnectionId:
            return "RETIRE_CONNECTION_ID";
        case FrameType::kPathChallenge:
            return "PATH_CHALLENGE";
        case FrameType::kPathResponse:
            return "PATH_RESPONSE";
        case FrameType::kConnectionClose:
            return "CONNECTION_CLOSE";
        case FrameType::kConnectionCloseApp:
            return "CONNECTION_CLOSE_APP";
        case FrameType::kHandshakeDone:
            return "HANDSHAKE_DONE";
        case FrameType::kResetStream:
            return "RESET_STREAM";
        case FrameType::kStopSending:
            return "STOP_SENDING";
        case FrameType::kStreamDataBlocked:
            return "STREAM_DATA_BLOCKED";
        case FrameType::kMaxStreamData:
            return "MAX_STREAM_DATA";
        default:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                return "STREAM_DATA";
            } else {
                LOG_ERROR("invalid frame type. type:%s", frame_type);
            }
    }
    return "UNKNOWN";
}

}  // namespace quic
}  // namespace quicx
