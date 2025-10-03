#ifndef UPGRADE_INCLUDE_IF_UPGRADE
#define UPGRADE_INCLUDE_IF_UPGRADE

#include <memory>
#include "upgrade/include/type.h"
#include "common/network/if_event_loop.h"

namespace quicx {
namespace upgrade {

// HTTP upgrade server interface
class IUpgrade {
public:
    IUpgrade() = default;
    virtual ~IUpgrade() = default;

    // Add a listener with specified settings
    virtual bool AddListener(UpgradeSettings& settings) = 0;

    // Create a server instance
    static std::unique_ptr<IUpgrade> MakeUpgrade(std::shared_ptr<common::IEventLoop> event_loop);
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_INCLUDE_IF_UPGRADE 