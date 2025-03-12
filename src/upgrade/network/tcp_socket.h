#ifndef UPGRADE_NETWORK_IF_SOCKET
#define UPGRADE_NETWORK_IF_SOCKET

#include <string>
#include <memory>
#include <cstdint>
#include "common/network/address.h"
#include "common/buffer/if_buffer_chains.h"
#include "upgrade/upgrade/if_socket_handler.h"

namespace quicx {
namespace upgrade {

class TcpSocket {
public:
    TcpSocket(uint64_t socket,
        common::Address remote_address,
        std::shared_ptr<ITcpAction> action,
        std::shared_ptr<ISocketHandler> handler,
        std::shared_ptr<common::BlockMemoryPool> pool_block);
    ~TcpSocket();

    uint32_t GetSocket() { return socket_; }
    const common::Address& GetRemoteAddress() { return remote_address_; }

    std::shared_ptr<common::IBufferChains> GetWriteBuffer() { return write_buffer_; }
    std::shared_ptr<common::IBufferChains> GetReadBuffer() { return read_buffer_; }
    std::shared_ptr<ISocketHandler> GetHandler() { return handler_; }
    std::shared_ptr<ITcpAction> GetAction() { return action_.lock(); }

    void SetContext(void* context) { context_ = context; }
    void* GetContext() { return context_; }

private:
    void* context_;
    uint64_t socket_;
    common::Address remote_address_;
    std::weak_ptr<ITcpAction> action_;
    std::shared_ptr<ISocketHandler> handler_;
    std::shared_ptr<common::IBufferChains> read_buffer_;
    std::shared_ptr<common::IBufferChains> write_buffer_;
};

}
}

#endif