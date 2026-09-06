#ifndef UPGRADE_INCLUDE_TYPE
#define UPGRADE_INCLUDE_TYPE

#include <cstdint>
#include <quicx/common/type.h>
#include <string>
#include <vector>

namespace quicx {

/**
 * @brief Configuration settings for HTTP/1.1, HTTP/2, and HTTP/3 upgrade
 */
struct UpgradeSettings {
    // Listening configuration
    std::string listen_addr_ = "0.0.0.0";  ///< Listening address
    uint16_t http_port_ = 80;              ///< HTTP port
    uint16_t https_port_ = 443;            ///< HTTPS port
    uint16_t h3_port_ = 443;               ///< HTTP/3 port

    // Protocol support flags
    bool enable_http1_ = true;                                                 ///< Enable HTTP/1.1
    bool enable_http2_ = true;                                                 ///< Enable HTTP/2
    bool enable_http3_ = true;                                                 ///< Enable HTTP/3
    std::vector<std::string> preferred_protocols_ = {"h3", "h2", "http/1.1"};  ///< Protocol preference order

    // TLS certificate configuration
    std::string cert_file_;     ///< Path to certificate file
    std::string key_file_;      ///< Path to private key file
    char* cert_pem_ = nullptr;  ///< Certificate in PEM format
    char* key_pem_ = nullptr;   ///< Private key in PEM format

    // Timeout settings
    uint32_t detection_timeout_ms_ = 5000;  ///< Protocol detection timeout
    uint32_t upgrade_timeout_ms_ = 10000;   ///< Protocol upgrade timeout

    // Logging configuration
    LogLevel log_level_ = LogLevel::kInfo;  ///< Logging level

    /**
     * @brief Check if HTTPS is enabled
     *
     * @return true if certificate configuration is provided
     */
    bool IsHTTPSEnabled() const {
        return !cert_file_.empty() || !key_file_.empty() || cert_pem_ != nullptr || key_pem_ != nullptr;
    }
};

}  // namespace quicx

#endif  // UPGRADE_INCLUDE_TYPE