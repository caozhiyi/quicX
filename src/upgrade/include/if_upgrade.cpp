#include "upgrade/include/if_upgrade.h"
#include "upgrade/server/upgrade_server.h"

namespace quicx {
namespace upgrade {

std::unique_ptr<IUpgrade> IUpgrade::Create() {
    return std::make_unique<UpgradeServer>();
}

} // namespace upgrade
} // namespace quicx 