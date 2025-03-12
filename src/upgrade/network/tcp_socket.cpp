#include "upgrade/network/tcp_socket.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {
namespace upgrade {

TcpSocket::TcpSocket(uint64_t socket,
    common::Address remote_address,
    std::shared_ptr<ITcpAction> action,
    std::shared_ptr<ISocketHandler> handler,
    std::shared_ptr<common::BlockMemoryPool> pool_block):
    context_(nullptr),
    socket_(socket),
    remote_address_(remote_address),
    action_(action),
    handler_(handler) {
    read_buffer_ = std::make_shared<common::BufferChains>(pool_block);
    write_buffer_ = std::make_shared<common::BufferChains>(pool_block);
}

TcpSocket::~TcpSocket() {

}

}
}