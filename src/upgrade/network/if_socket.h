#ifndef UPGRADE_NETWORK_IF_SOCKET
#define UPGRADE_NETWORK_IF_SOCKET

#include <string>
#include <memory>
#include <cstdint>
#include "common/network/address.h"
#include "common/buffer/if_buffer.h"
namespace quicx {
namespace upgrade {

class ISocket {
public:
    ISocket() {}
    virtual ~ISocket() {}

    virtual uint32_t GetSocket() = 0;

    virtual common::Address GetLocalAddress() = 0;
    virtual common::Address GetRemoteAddress() = 0;

    virtual void SetWriteBuffer(std::shared_ptr<common::IBuffer> buffer) = 0;
    virtual std::shared_ptr<common::IBuffer> GetWriteBuffer() = 0;

    virtual void SetReadBuffer(std::shared_ptr<common::IBuffer> buffer) = 0;
    virtual std::shared_ptr<common::IBuffer> GetReadBuffer() = 0;

    virtual void SetHandler(std::shared_ptr<IfSocketHandler> handler) = 0;
    virtual std::shared_ptr<IfSocketHandler> GetHandler() = 0;

    virtual void Send(std::shared_ptr<common::IBufferWrite> buffer) = 0;
};

}
}

#endif
