#include <thread>
#include <sstream>

#include "common/log/log.h"
#include "quic/quicx/worker.h"
#include "quic/udp/if_sender.h"
#include "quic/common/version.h"
#include "common/buffer/buffer.h"
#include "quic/packet/init_packet.h"

namespace quicx {
namespace quic {

Worker::Worker(const QuicConfig& config, 
        std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<ISender> sender,
        const QuicTransportParams& params,
        std::shared_ptr<common::IEventLoop> event_loop,
        connection_state_callback connection_handler):
    IWorker(),
    ctx_(ctx),
    params_(params),
    sender_(sender),
    connection_handler_(connection_handler),
    event_loop_(event_loop),
    active_send_connection_set_1_is_current_(true) {

    ecn_enabled_ = config.enable_ecn_;
}

Worker::~Worker() {

}

void Worker::HandlePacket(PacketInfo& packet_info) {
    if (packet_info.net_packet_ && packet_info.net_packet_->GetTime() > 0 && !packet_info.packets_.empty()) {
        InnerHandlePacket(packet_info);
    }
}

std::shared_ptr<common::IEventLoop> Worker::GetEventLoop() {
    return event_loop_;
}

std::string Worker::GetWorkerId() {
    if (worker_id_.empty()) {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        worker_id_ = oss.str();
    }
    return worker_id_;
}

void Worker::Process() {
    ProcessSend();
}

void Worker::ProcessSend() {
    static thread_local uint8_t buf[1500] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));

    std::shared_ptr<NetPacket> packet = std::make_shared<NetPacket>();
    SendOperation send_operation;
    
    SwitchActiveSendConnectionSet(); 
    auto& cur_active_send_connection_set = GetReadActiveSendConnectionSet();
    if (cur_active_send_connection_set.empty()) {
        return;
    }

    for (auto iter = cur_active_send_connection_set.begin(); iter != cur_active_send_connection_set.end();) {
        if (!(*iter)->GenerateSendData(buffer, send_operation)) {
            common::LOG_ERROR("generate send data failed.");
            iter = cur_active_send_connection_set.erase(iter);
            continue;
        }

        if (buffer->GetDataLength() == 0) {
            common::LOG_WARN("generate send data length is 0.");
            iter = cur_active_send_connection_set.erase(iter);
            continue;
        }

        packet->SetData(buffer);
        packet->SetAddress((*iter)->GetPeerAddress());
        packet->SetSocket((*iter)->GetSocket()); // client connection will always -1

        if (!sender_->Send(packet)) {
            common::LOG_ERROR("udp send failed.");
        }
        buffer->Clear();
        switch (send_operation) {
            case SendOperation::kAllSendDone:
                iter = cur_active_send_connection_set.erase(iter);
                break;
            case SendOperation::kNextPeriod:
                iter++;
                break;
            case SendOperation::kSendAgainImmediately: // do nothing, send again immediately
            default:
                break;
        }
    }
}

bool Worker::InitPacketCheck(std::shared_ptr<IPacket> packet) {
    if (packet->GetHeader()->GetPacketType() != PacketType::kInitialPacketType) {
        common::LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    // check init packet length according to RFC9000
    // Initial packets MUST be sent with a payload length of at least 1200 bytes
    // unless the client knows that the server supports a larger minimum PMTU
    if (packet->GetSrcBuffer().GetLength() < 1200) {
        common::LOG_ERROR("init packet length too small. length:%d", packet->GetSrcBuffer().GetLength());
        return false;
    }

    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    uint32_t version = ((LongHeader*)init_packet->GetHeader())->GetVersion();
    if (!VersionCheck(version)) {
        return false;
    }

    return true;
}

void Worker::HandleAddConnectionId(ConnectionID& cid, std::shared_ptr<IConnection> conn) {
    conn_map_[cid.Hash()] = conn;
    common::LOG_DEBUG("add connection id to client worker. cid:%llu", cid.Hash());
    if (auto notify = connection_id_notify_.lock()) {
        notify->AddConnectionID(cid, GetWorkerId());
    }
}

void Worker::HandleRetireConnectionId(ConnectionID& cid) {
    conn_map_.erase(cid.Hash());
    if (auto notify = connection_id_notify_.lock()) {
        notify->RetireConnectionID(cid, GetWorkerId());
    }
}

void Worker::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    if (connecting_set_.find(conn) != connecting_set_.end()) {
        connecting_set_.erase(conn);
        conn_map_[conn->GetConnectionIDHash()] = conn;
        connection_handler_(conn, ConnectionOperation::kConnectionCreate, 0, "");
    }
}

void Worker::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    GetWriteActiveSendConnectionSet().insert(conn);
    common::LOG_DEBUG("HandleActiveSendConnection");
    do_send_ = true;
    event_loop_->Wakeup();
}

void Worker::HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    conn_map_.erase(conn->GetConnectionIDHash());
    if (error > 0) {
        connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
    } else {
        connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
    }
}

std::unordered_set<std::shared_ptr<IConnection>>& Worker::GetReadActiveSendConnectionSet() {
    return active_send_connection_set_1_is_current_ ? active_send_connection_set_1_ : active_send_connection_set_2_;
}

std::unordered_set<std::shared_ptr<IConnection>>& Worker::GetWriteActiveSendConnectionSet() {
    return active_send_connection_set_1_is_current_ ? active_send_connection_set_2_ : active_send_connection_set_1_;
}

void Worker::SwitchActiveSendConnectionSet() {
    if (active_send_connection_set_1_is_current_) {
        active_send_connection_set_2_.insert(active_send_connection_set_1_.begin(), active_send_connection_set_1_.end());
        active_send_connection_set_1_.clear();
        active_send_connection_set_1_is_current_ = false;

    } else {
        active_send_connection_set_1_.insert(active_send_connection_set_2_.begin(), active_send_connection_set_2_.end());
        active_send_connection_set_2_.clear();
        active_send_connection_set_1_is_current_ = true;
    }
}

}
}