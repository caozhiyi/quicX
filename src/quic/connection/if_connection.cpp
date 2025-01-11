
#include "quic/connection/if_connection.h"
namespace quicx {
namespace quic {


IConnection::IConnection(std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t cid_hash)> retire_conn_id_cb):
        active_connection_cb_(active_connection_cb),
        handshake_done_cb_(handshake_done_cb),
        add_conn_id_cb_(add_conn_id_cb),
        retire_conn_id_cb_(retire_conn_id_cb) {}
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

void IConnection::SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb) { 
    active_connection_cb_ = cb;
}

void IConnection::SetHandshakeDoneCB(std::function<void(std::shared_ptr<IConnection>)> cb) {
    // if handshake is done, should not set again
    if (handshake_done_cb_) {
        handshake_done_cb_ = cb;
    }
}

void IConnection::SetAddConnectionIdCB(std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> cb) {
    add_conn_id_cb_ = cb;
}

void IConnection::SetRetireConnectionIdCB(std::function<void(uint64_t cid_hash)> cb) {
    retire_conn_id_cb_ = cb;
}

void IConnection::SetConnectionCloseCB(std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> cb) {
    connection_close_cb_ = cb;
}

}
}