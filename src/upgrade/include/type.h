#ifndef UPGRADE_INCLUDE_TYPE
#define UPGRADE_INCLUDE_TYPE

#include <string>
#include <cstdint>

namespace quicx {
namespace upgrade {


// log level
enum class LogLevel: uint8_t {
    kNull   = 0x00, // not print log
    kFatal  = 0x01,
    kError  = 0x02 | kFatal,
    kWarn   = 0x04 | kError,
    kInfo   = 0x08 | kWarn,
    kDebug  = 0x10 | kInfo,
};

// upgrade settings
struct UpgradeSettings {
    uint16_t upgrade_h3_port = 443;
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 80;
    
    std::string cert_file;
    std::string key_file;
    char* cert_pem = nullptr;
    char* key_pem = nullptr;
    
};

}
}

#endif
