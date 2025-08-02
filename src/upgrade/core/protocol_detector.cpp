#include "upgrade/core/protocol_detector.h"
#include <algorithm>
#include <cstring>

namespace quicx {
namespace upgrade {

Protocol ProtocolDetector::Detect(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return Protocol::UNKNOWN;
    }
    
    // Check if it's an HTTP/2 connection
    if (IsHTTP2(data)) {
        return Protocol::HTTP2;
    }
    
    // Check if it's an HTTP/1.1 connection
    if (IsHTTP1_1(data)) {
        return Protocol::HTTP1_1;
    }
    
    return Protocol::UNKNOWN;
}

bool ProtocolDetector::IsHTTP1_1(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        return false;
    }
    
    // Check HTTP/1.1 request line characteristics
    std::string prefix(data.begin(), data.begin() + std::min(static_cast<size_t>(20), data.size()));
    
    // Check common HTTP methods
    std::vector<std::string> http_methods = {"GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS "};
    for (const auto& method : http_methods) {
        if (prefix.find(method) == 0) {
            return true;
        }
    }
    
    // Check HTTP response line
    if (prefix.find("HTTP/") == 0) {
        return true;
    }
    
    return false;
}

bool ProtocolDetector::IsHTTP2(const std::vector<uint8_t>& data) {
    if (data.size() < 24) {
        return false;
    }
    
    // HTTP/2 connection preface: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    const std::vector<uint8_t> http2_preface = {
        0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a,
        0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a
    };
    
    return std::equal(http2_preface.begin(), http2_preface.end(), data.begin());
}

bool ProtocolDetector::HasALPN(const std::vector<uint8_t>& data) {
    // Look for ALPN extension in TLS ClientHello
    // Simplified implementation - actual implementation needs to parse TLS handshake
    return ExtractALPNData(data).length() > 0;
}

std::string ProtocolDetector::ExtractALPNData(const std::vector<uint8_t>& data) {
    // Simplified implementation - actual implementation needs to parse TLS ClientHello completely
    // Returns empty string indicating no ALPN data found
    return "";
}

std::vector<std::string> ProtocolDetector::ParseALPNProtocols(const std::vector<uint8_t>& data) {
    std::vector<std::string> protocols;
    
    // Simplified implementation - actual implementation needs to parse TLS ALPN extension
    // Returns default supported protocol list
    protocols = {"h3", "h2", "http/1.1"};
    
    return protocols;
}

} // namespace upgrade
} // namespace quicx 