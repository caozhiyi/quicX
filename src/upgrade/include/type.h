#ifndef UPGRADE_INCLUDE_TYPE
#define UPGRADE_INCLUDE_TYPE

#include <string>
#include <vector>
#include <cstdint>

namespace quicx {
namespace upgrade {

// Log level enumeration
enum class LogLevel: uint8_t {
    kNull   = 0x00, // No logging
    kFatal  = 0x01,
    kError  = 0x02 | kFatal,
    kWarn   = 0x04 | kError,
    kInfo   = 0x08 | kWarn,
    kDebug  = 0x10 | kInfo,
};

// Upgrade configuration settings
struct UpgradeSettings {
    // Listening configuration
    std::string listen_addr = "0.0.0.0";
    uint16_t http_port = 80;
    uint16_t https_port = 443;
    uint16_t h3_port = 443;
    
    // Protocol support flags
    bool enable_http1 = true;
    bool enable_http2 = true;
    bool enable_http3 = true;
    std::vector<std::string> preferred_protocols = {"h3", "h2", "http/1.1"};
    
    // TLS certificate configuration
    std::string cert_file;
    std::string key_file;
    char* cert_pem = nullptr;
    char* key_pem = nullptr;
    
    // Timeout settings
    uint32_t detection_timeout_ms = 5000;
    uint32_t upgrade_timeout_ms = 10000;
    
    // Logging configuration
    LogLevel log_level = LogLevel::kInfo;
    
    // Check if HTTPS is enabled based on certificate configuration
    bool IsHTTPSEnabled() const {
        return !cert_file.empty() || !key_file.empty() || cert_pem != nullptr || key_pem != nullptr;
    }
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_INCLUDE_TYPE_H 