#ifndef UPGRADE_INCLUDE_IF_UPGRADE
#define UPGRADE_INCLUDE_IF_UPGRADE

#include <string>
#include <memory>
#include <cstdint>
#include "upgrade/include/type.h"

namespace quicx {
namespace upgrade {

// HTTP3 client interface
class IUpgrade {
public:
    IUpgrade() {}
    virtual ~IUpgrade() {};

    // Initialize the server with a certificate and a key
    virtual bool Init(LogLevel level = LogLevel::kNull) = 0;

    // Start the server
    virtual bool Start(UpgradeSettings& settings) = 0;

    // Stop the server
    virtual void Stop() = 0;

    // Join the server
    virtual void Join() = 0;

    // Create a server instance
    static std::unique_ptr<IUpgrade> Create();
};

}
}

#endif
