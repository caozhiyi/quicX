
#include "quic/connection/if_connection.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

IConnection::IConnection(std::shared_ptr<ISender> sender,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(ConnectionID&)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t, const std::string&)> connection_close_cb):
    active_connection_cb_(active_connection_cb),
    handshake_done_cb_(handshake_done_cb),
    add_conn_id_cb_(add_conn_id_cb),
    retire_conn_id_cb_(retire_conn_id_cb),
    connection_close_cb_(connection_close_cb),
    sockfd_(-1) {}

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

void IConnection::SendImmediate(std::shared_ptr<common::IBuffer> buffer) {
    if (!buffer || buffer->GetDataLength() == 0) {
        common::LOG_WARN("SendImmediate: invalid buffer or empty data");
        return;
    }

    std::shared_ptr<NetPacket> packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    packet->SetData(buffer);
    packet->SetAddress(peer_addr_);
    packet->SetSocket(sockfd_);

    if (!sender_->Send(packet)) {
        common::LOG_ERROR("SendImmediate: udp send failed");
        return;
    }

    common::LOG_DEBUG("SendImmediate: sent %zu bytes to %s", buffer->GetDataLength(), peer_addr_.AsString().c_str());
}

void IConnection::SendDeferred() {
    if (active_connection_cb_) {
        active_connection_cb_(shared_from_this());
    }
}

bool IConnection::DoSend() {
    std::shared_ptr<NetPacket> packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto buffer = packet->GetData();

    while (true) {
        SendOperation send_operation;
        if (!GenerateSendData(buffer, send_operation)) {
            common::LOG_ERROR("generate send data failed.");
            return true;  // don't need to send again
        }

        if (buffer->GetDataLength() == 0) {
            common::LOG_WARN("generate send data length is 0.");
            return true;  // don't need to send again
        }

        packet->SetData(buffer);
        // select destination address from connection (future: candidate path probing)
        packet->SetAddress(AcquireSendAddress());
        packet->SetSocket(GetSocket());  // client connection will always -1

        if (!sender_->Send(packet)) {
            common::LOG_ERROR("udp send failed.");
            return true;  // don't need to send again
        }
        buffer->Clear();
        switch (send_operation) {
            case SendOperation::kAllSendDone:
                return true;  // sent done
            case SendOperation::kNextPeriod:
                return false;  // need to send again later
            case SendOperation::kSendAgainImmediately:
                break;  // do nothing, send again immediately
            default:
                common::LOG_ERROR("invalid send operation.");
                return true;  // sent done
        }
    }
    return false;  // should not reach here
}

}  // namespace quic
}  // namespace quicx