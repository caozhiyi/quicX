#ifndef UPGRADE_UPGRADE_UPGRADE_SERVER
#define UPGRADE_UPGRADE_UPGRADE_SERVER

#include "upgrade/include/if_upgrade.h"
#include "upgrade/network/if_tcp_action.h"

namespace quicx {
namespace upgrade {

class UpgradeServer:
    public IUpgrade {
public:
    UpgradeServer() {}
    virtual ~UpgradeServer() {}

    virtual bool Init(LogLevel level = LogLevel::kNull) override;

    virtual bool AddListener(UpgradeSettings& settings) override;
    virtual void Stop() override;
    virtual void Join() override;

private:
    std::shared_ptr<ITcpAction> tcp_action_;
};

}
}

#endif