
#include "common/network/io_handle.h"
#include "quic/connection/if_connection.h"

namespace quicx {
namespace quic {

IConnection::IConnection(std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t, const std::string&)> connection_close_cb):
        active_connection_cb_(active_connection_cb),
        handshake_done_cb_(handshake_done_cb),
        add_conn_id_cb_(add_conn_id_cb),
        retire_conn_id_cb_(retire_conn_id_cb),
        connection_close_cb_(connection_close_cb),
        sockfd_(-1),
        migration_sockfd_(-1) {

}

IConnection::~IConnection() {

}

void IConnection::GetRemoteAddr(std::string& addr, uint32_t& port) {
    addr = peer_addr_.GetIp();
    port = peer_addr_.GetPort();
}

void IConnection::SetPeerAddress(const common::Address& addr) {
    peer_addr_ = addr;
}

void IConnection::SetPeerAddress(const common::Address&& addr) {
    peer_addr_ = std::move(addr); 
}

const common::Address& IConnection::GetPeerAddress() {
    return peer_addr_;
}

void IConnection::GetLocalAddr(std::string& addr, uint32_t& port) {
    // If we have a cached local address, use it
    if (!local_addr_.GetIp().empty()) {
        addr = local_addr_.GetIp();
        port = local_addr_.GetPort();
        return;
    }
    
    // Otherwise, query from socket
    int32_t sock = (migration_sockfd_ > 0) ? migration_sockfd_ : sockfd_;
    if (sock > 0) {
        common::Address local;
        if (GetLocalAddressFromSocket(sock, local)) {
            local_addr_ = local;
            addr = local_addr_.GetIp();
            port = local_addr_.GetPort();
            return;
        }
    }
    
    addr = "";
    port = 0;
}

bool IConnection::GetLocalAddressFromSocket(int32_t sockfd, common::Address& addr) {
    if (sockfd <= 0) {
        return false;
    }
    return common::ParseLocalAddress(sockfd, addr);
}

}
}