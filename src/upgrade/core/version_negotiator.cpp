#include <algorithm>
#include "upgrade/core/protocol_detector.h"
#include "upgrade/core/version_negotiator.h"

namespace quicx {
namespace upgrade {

NegotiationResult VersionNegotiator::Negotiate(
    ConnectionContext& context, const UpgradeSettings& settings) {
    
    // Step 1: Protocol detection (skip if already known)
    if (context.detected_protocol == Protocol::UNKNOWN) {
        if (!DetectProtocol(context)) {
            return {false, Protocol::UNKNOWN, "", {}, "Protocol detection failed"};
        }
    }
    
    // Step 2: Select optimal protocol
    Protocol target = SelectBestProtocol(context, settings);
    context.target_protocol = target;
    
    // Step 3: Generate upgrade strategy
    return GenerateUpgradeStrategy(context, settings);
}

bool VersionNegotiator::DetectProtocol(ConnectionContext& context) {
    if (context.initial_data.empty()) {
        return false;
    }
    
    context.detected_protocol = ProtocolDetector::Detect(context.initial_data);
    return context.detected_protocol != Protocol::UNKNOWN;
}

Protocol VersionNegotiator::SelectBestProtocol(const ConnectionContext& context, const UpgradeSettings& settings) {
    // Always prefer HTTP/3 if client already using it
    if (context.detected_protocol == Protocol::HTTP3) {
        return Protocol::HTTP3;
    }

    // Prefer HTTP/3 if advertised via ALPN
    if (SupportsProtocol(context.alpn_protocols, "h3")) {
        return Protocol::HTTP3;
    }

    // Prefer upgrading HTTP/1.1 or HTTP/2 to HTTP/3 if enabled
    if (settings.enable_http3 &&
        (context.detected_protocol == Protocol::HTTP1_1 || context.detected_protocol == Protocol::HTTP2)) {
        return Protocol::HTTP3;
    }

    // Otherwise, keep detected protocol
    return context.detected_protocol;
}

NegotiationResult VersionNegotiator::GenerateUpgradeStrategy(
    ConnectionContext& context, const UpgradeSettings& settings) {
    
    NegotiationResult result;
    result.target_protocol = context.target_protocol;
    
    // If target protocol is the same as detected protocol, no upgrade needed
    if (context.target_protocol == context.detected_protocol) {
        result.success = true;
        return result;
    }
    
    // Only support upgrade to HTTP/3
    if (context.target_protocol == Protocol::HTTP3) {
        result.success = true;
        result.upgrade_token = "h3";
        
        // Generate upgrade data based on detected protocol
        switch (context.detected_protocol) {
            case Protocol::HTTP1_1:
                result.upgrade_data = GenerateHTTP1UpgradeData(settings);
                break;
            case Protocol::HTTP2:
                result.upgrade_data = GenerateHTTP2UpgradeData(settings);
                break;
            case Protocol::HTTP3:
                // Direct HTTP/3: no upgrade payload required
                result.upgrade_data.clear();
                break;
            default:
                result.success = false;
                result.error_message = "Unsupported upgrade path to HTTP/3";
                break;
        }
    } else {
        result.success = false;
        result.error_message = "Only HTTP/3 upgrades are supported";
    }
    
    return result;
}

bool VersionNegotiator::SupportsProtocol(const std::vector<std::string>& client_protocols, const std::string& protocol) {
    return std::find(client_protocols.begin(), client_protocols.end(), protocol) != client_protocols.end();
}

std::vector<uint8_t> VersionNegotiator::GenerateHTTP1UpgradeData(const UpgradeSettings& settings) {
    // For browsers over plain HTTP, many do not switch on 101 with Upgrade: h3.
    // Prefer responding 200 OK with Alt-Svc to advertise h3 endpoint.
    // Example minimal response with Alt-Svc: h3=":<port>"; ma=86400

    std::string body = "h3 available on :" + std::to_string(settings.h3_port) + "\n";
    std::string alt_svc = "h3=\":" + std::to_string(settings.h3_port) + "\"; ma=86400";

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Alt-Svc: " + alt_svc + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    return std::vector<uint8_t>(response.begin(), response.end());
}

std::vector<uint8_t> VersionNegotiator::GenerateHTTP2UpgradeData(const UpgradeSettings& settings) {
    // Build minimal, readable HTTP/2 frames to steer client to HTTP/3:
    // 1) SETTINGS (type=0x04, empty payload) as server preface
    // 2) ALTSVC (type=0x0a) advertising h3 on the configured port

    std::vector<uint8_t> out;
    out.reserve(9 /*SETTINGS*/ + 9 /*hdr*/ + 2 + 32 /*value approx*/);

    // SETTINGS frame: length=0, type=0x04, flags=0x00, stream=0
    out.push_back(0x00); // length[0]
    out.push_back(0x00); // length[1]
    out.push_back(0x00); // length[2]
    out.push_back(0x04); // type SETTINGS
    out.push_back(0x00); // flags
    out.push_back(0x00); // stream id
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);

    // ALTSVC frame per RFC 7838: payload = Origin-Len(2) | Origin | Field-Value
    const uint16_t origin_len = 0; // connection-wide
    std::string alt_svc_value = "h3=\":" + std::to_string(settings.h3_port) + "\"; ma=86400";
    uint32_t payload_len = 2 + static_cast<uint32_t>(alt_svc_value.size());

    // Frame header: Length(24) | Type(0x0a) | Flags(0x00) | StreamID(0)
    out.push_back(static_cast<uint8_t>((payload_len >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(payload_len & 0xFF));
    out.push_back(0x0a); // ALTSVC
    out.push_back(0x00); // flags
    out.push_back(0x00); // stream id
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);

    // Payload
    out.push_back(static_cast<uint8_t>((origin_len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(origin_len & 0xFF));
    out.insert(out.end(), alt_svc_value.begin(), alt_svc_value.end());

    return out;
}

} // namespace upgrade
} // namespace quicx 