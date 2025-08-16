#ifndef UPGRADE_INCLUDE_IF_UPGRADE
#define UPGRADE_INCLUDE_IF_UPGRADE

#include <memory>
#include "upgrade/include/type.h"

namespace quicx {
namespace upgrade {

// HTTP upgrade server interface
class IUpgrade {
public:
    IUpgrade() = default;
    virtual ~IUpgrade() = default;

    // Initialize the upgrade server
    virtual bool Init(LogLevel level = LogLevel::kNull) = 0;

    // Add a listener with specified settings
    virtual bool AddListener(UpgradeSettings& settings) = 0;

    // Stop the upgrade server
    virtual void Stop() = 0;

    // Wait for the server to finish
    virtual void Join() = 0;

    // Create a server instance
    static std::unique_ptr<IUpgrade> MakeUpgrade();
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_INCLUDE_IF_UPGRADE 