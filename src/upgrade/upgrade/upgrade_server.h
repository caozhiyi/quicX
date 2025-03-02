#ifndef UPGRADE_UPGRADE_UPGRADE_SERVER
#define UPGRADE_UPGRADE_UPGRADE_SERVER

#include "upgrade/include/if_upgrade.h"

namespace quicx {
namespace upgrade {

class UpgradeServer: public IUpgrade {
public:
    UpgradeServer() {}
    virtual ~UpgradeServer() {}

    virtual bool Init(LogLevel level = LogLevel::kNull) override;

    virtual bool Start(UpgradeSettings& settings) override;
    virtual void Stop() override;
    virtual void Join() override;
};

}
}

#endif