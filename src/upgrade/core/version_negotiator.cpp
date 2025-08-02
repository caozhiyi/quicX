#include <sstream>
#include <algorithm>
#include "upgrade/core/protocol_detector.h"
#include "upgrade/core/version_negotiator.h"

namespace quicx {
namespace upgrade {

NegotiationResult VersionNegotiator::Negotiate(
    ConnectionContext& context, const UpgradeSettings& settings) {
    
    // Step 1: Protocol detection
    if (!DetectProtocol(context)) {
        return {false, Protocol::UNKNOWN, "", {}, "Protocol detection failed"};
    }
    
    // Step 2: ALPN parsing
    ParseALPN(context);
    
    // Step 3: Select optimal protocol
    Protocol target = SelectBestProtocol(context, settings);
    context.target_protocol = target;
    
    // Step 4: Generate upgrade strategy
    return GenerateUpgradeStrategy(context, settings);
}

bool VersionNegotiator::DetectProtocol(ConnectionContext& context) {
    if (context.initial_data.empty()) {
        return false;
    }
    
    context.detected_protocol = ProtocolDetector::Detect(context.initial_data);
    return context.detected_protocol != Protocol::UNKNOWN;
}

bool VersionNegotiator::ParseALPN(ConnectionContext& context) {
    if (context.initial_data.empty()) {
        return false;
    }
    
    context.alpn_protocols = ProtocolDetector::ParseALPNProtocols(context.initial_data);
    return !context.alpn_protocols.empty();
}

Protocol VersionNegotiator::SelectBestProtocol(const ConnectionContext& context, const UpgradeSettings& settings) {
    // If QUIC is detected, return HTTP/3 directly
    if (context.detected_protocol == Protocol::HTTP3) {
        return Protocol::HTTP3;
    }
    
    // Select optimal protocol based on client capabilities and server configuration
    if (settings.enable_http3 && SupportsProtocol(context.alpn_protocols, "h3")) {
        return Protocol::HTTP3;
    }
    
    if (settings.enable_http2 && SupportsProtocol(context.alpn_protocols, "h2")) {
        return Protocol::HTTP2;
    }
    
    if (settings.enable_http1) {
        return Protocol::HTTP1_1;
    }
    
    // Default to detected protocol
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
    
    // Generate upgrade strategy based on detected protocol
    switch (context.detected_protocol) {
        case Protocol::HTTP1_1:
            if (context.target_protocol == Protocol::HTTP3) {
                result.success = true;
                result.upgrade_token = "h3";
                result.upgrade_data = GenerateHTTP1UpgradeData();
            }
            break;
            
        case Protocol::HTTP2:
            if (context.target_protocol == Protocol::HTTP3) {
                result.success = true;
                result.upgrade_token = "h3";
                result.upgrade_data = GenerateHTTP2UpgradeData();
            }
            break;
            
        default:
            result.success = false;
            result.error_message = "Unsupported upgrade path";
            break;
    }
    
    return result;
}

bool VersionNegotiator::SupportsProtocol(const std::vector<std::string>& client_protocols, const std::string& protocol) {
    return std::find(client_protocols.begin(), client_protocols.end(), protocol) != client_protocols.end();
}

std::vector<uint8_t> VersionNegotiator::GenerateHTTP1UpgradeData() {
    // Generate HTTP/1.1 upgrade response data
    std::string response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: h3\r\n"
        "Connection: Upgrade\r\n"
        "\r\n";
    
    return std::vector<uint8_t>(response.begin(), response.end());
}

std::vector<uint8_t> VersionNegotiator::GenerateHTTP2UpgradeData() {
    // Generate HTTP/2 GOAWAY frame data to indicate upgrade to HTTP/3
    // Simplified implementation - actual implementation needs to generate complete HTTP/2 frame
    std::vector<uint8_t> goaway_frame = {
        0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    return goaway_frame;
}

} // namespace upgrade
} // namespace quicx 