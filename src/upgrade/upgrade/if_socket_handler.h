#ifndef UPGRADE_UPGRADE_IF_SOCKET_HANDLER
#define UPGRADE_UPGRADE_IF_SOCKET_HANDLER

#include "upgrade/network/if_socket.h"
#include "upgrade/include/if_upgrade.h"
#include "common/buffer/if_buffer_read.h"


namespace quicx {
namespace upgrade {

class IfSocketHandler {
public:
    IfSocketHandler() {}
    virtual ~IfSocketHandler() {}

    virtual void HandleSocketConnect(std::shared_ptr<ISocket> socket) = 0;
    virtual void HandleSocketData(std::shared_ptr<ISocket> socket, std::shared_ptr<common::IBufferRead> buffer) = 0;
};

}
}


#endif