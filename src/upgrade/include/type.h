#ifndef UPGRADE_INCLUDE_TYPE
#define UPGRADE_INCLUDE_TYPE

#include <string>
#include <vector>
#include <cstdint>

#include "common/include/type.h"

namespace quicx {
namespace upgrade {

/**
 * @brief Configuration settings for HTTP/1.1, HTTP/2, and HTTP/3 upgrade
 */
struct UpgradeSettings {
    // Listening configuration
    std::string listen_addr = "0.0.0.0";  ///< Listening address
    uint16_t http_port = 80;   ///< HTTP port
    uint16_t https_port = 443; ///< HTTPS port
    uint16_t h3_port = 443;    ///< HTTP/3 port
    
    // Protocol support flags
    bool enable_http1 = true;  ///< Enable HTTP/1.1
    bool enable_http2 = true;  ///< Enable HTTP/2
    bool enable_http3 = true;  ///< Enable HTTP/3
    std::vector<std::string> preferred_protocols = {"h3", "h2", "http/1.1"};  ///< Protocol preference order
    
    // TLS certificate configuration
    std::string cert_file;  ///< Path to certificate file
    std::string key_file;   ///< Path to private key file
    char* cert_pem = nullptr;  ///< Certificate in PEM format
    char* key_pem = nullptr;   ///< Private key in PEM format
    
    // Timeout settings
    uint32_t detection_timeout_ms = 5000;  ///< Protocol detection timeout
    uint32_t upgrade_timeout_ms = 10000;   ///< Protocol upgrade timeout
    
    // Logging configuration
    LogLevel log_level = LogLevel::kInfo;  ///< Logging level
    
    /**
     * @brief Check if HTTPS is enabled
     *
     * @return true if certificate configuration is provided
     */
    bool IsHTTPSEnabled() const {
        return !cert_file.empty() || !key_file.empty() || cert_pem != nullptr || key_pem != nullptr;
    }
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_INCLUDE_TYPE_H 