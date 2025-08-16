#ifndef UPGRADE_CORE_VERSION_NEGOTIATOR
#define UPGRADE_CORE_VERSION_NEGOTIATOR

#include <string>
#include <vector>
#include "upgrade/include/type.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {

// Version negotiation result structure
struct NegotiationResult {
    bool success = false;
    Protocol target_protocol = Protocol::UNKNOWN;
    std::string upgrade_token;
    std::vector<uint8_t> upgrade_data;
    std::string error_message;
};

// Version negotiation utility class
class VersionNegotiator {
public:
    // Perform version negotiation for a connection
    static NegotiationResult Negotiate(ConnectionContext& context, const UpgradeSettings& settings);
    
private:
    // Detect protocol from connection context
    static bool DetectProtocol(ConnectionContext& context);
    
    // Select the best protocol based on client capabilities and server settings
    static Protocol SelectBestProtocol(const ConnectionContext& context, const UpgradeSettings& settings);
    
    // Generate upgrade strategy based on detected and target protocols
    static NegotiationResult GenerateUpgradeStrategy(ConnectionContext& context, const UpgradeSettings& settings);
    
    // Check if client supports a specific protocol
    static bool SupportsProtocol(const std::vector<std::string>& client_protocols, const std::string& protocol);
    
    // Generate HTTP/1.1 upgrade data
    static std::vector<uint8_t> GenerateHTTP1UpgradeData(const UpgradeSettings& settings);
    
    // Generate HTTP/2 upgrade data
    static std::vector<uint8_t> GenerateHTTP2UpgradeData(const UpgradeSettings& settings);
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_VERSION_NEGOTIATOR_H 