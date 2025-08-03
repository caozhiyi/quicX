#ifndef UPGRADE_CORE_PROTOCOL_DETECTOR_H
#define UPGRADE_CORE_PROTOCOL_DETECTOR_H

#include <vector>
#include <string>
#include "upgrade/core/connection_state.h"

namespace quicx {
namespace upgrade {

// Protocol detection utility class
class ProtocolDetector {
public:
    // Detect protocol from initial data
    static Protocol Detect(const std::vector<uint8_t>& data);
    
private:
    // Check if data represents HTTP/1.1 protocol
    static bool IsHTTP1_1(const std::vector<uint8_t>& data);
    
    // Check if data represents HTTP/2 protocol
    static bool IsHTTP2(const std::vector<uint8_t>& data);
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_PROTOCOL_DETECTOR_H 