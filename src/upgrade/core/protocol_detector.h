#ifndef UPGRADE_CORE_PROTOCOL_DETECTOR
#define UPGRADE_CORE_PROTOCOL_DETECTOR

#include <vector>
#include "upgrade/handlers/connection_context.h"

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