#ifndef UPGRADE_NETWORK_IF_SOCKET
#define UPGRADE_NETWORK_IF_SOCKET

#include <string>
#include <memory>
#include <cstdint>
#include "common/network/address.h"
#include "common/buffer/if_buffer_write.h"
namespace quicx {
namespace upgrade {

class ISocket {
public:
    ISocket() {}
    virtual ~ISocket() {}

    virtual uint32_t GetSocket() = 0;

    virtual common::Address GetLocalAddress() = 0;
    virtual common::Address GetRemoteAddress() = 0;

    virtual void Send(std::shared_ptr<common::IBufferWrite> buffer) = 0;
};

}
}

#endif
