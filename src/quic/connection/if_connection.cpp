
#include "quic/connection/if_connection.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace quic {

IConnection::IConnection(const ConnectionCallbacks& callbacks):
    add_conn_id_cb_(callbacks.add_conn_id_cb),
    retire_conn_id_cb_(callbacks.retire_conn_id_cb),
    active_connection_cb_(callbacks.active_connection_cb),
    handshake_done_cb_(callbacks.handshake_done_cb),
    connection_close_cb_(callbacks.connection_close_cb) {}

IConnection::~IConnection() {}

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
    // Only the cached value is reachable at this level: the active socket fd is
    // owned by DatagramEmitter, so the socket-query fallback lives in
    // BaseConnection::GetLocalAddr, which overrides this.
    if (!local_addr_.GetIp().empty()) {
        addr = local_addr_.GetIp();
        port = local_addr_.GetPort();
        return;
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

}  // namespace quic
}  // namespace quicx