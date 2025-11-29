#include <sstream>
#include <thread>

#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/packet/init_packet.h"
#include "quic/quicx/global_resource.h"
#include "quic/quicx/worker.h"
#include "quic/udp/if_sender.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

Worker::Worker(const QuicConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
    const QuicTransportParams& params, connection_state_callback connection_handler,
    std::shared_ptr<common::IEventLoop> event_loop):
    IWorker(),
    ctx_(ctx),
    params_(params),
    sender_(sender),
    connection_handler_(connection_handler),
    active_send_connection_set_1_is_current_(true),
    event_loop_(event_loop) {
    ecn_enabled_ = config.enable_ecn_;
}

Worker::~Worker() {}

void Worker::HandlePacket(PacketParseResult& packet_info) {
    if (packet_info.net_packet_ && packet_info.net_packet_->GetTime() > 0 && !packet_info.packets_.empty()) {
        InnerHandlePacket(packet_info);
    }
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
    common::LOG_DEBUG("Worker::Process called");
    ProcessSend();
}

void Worker::ProcessSend() {
    SwitchActiveSendConnectionSet();
    auto& cur_active_send_connection_set = GetReadActiveSendConnectionSet();
    common::LOG_DEBUG(
        "Worker::ProcessSend: active_send_connection_set size: %zu", cur_active_send_connection_set.size());
    if (cur_active_send_connection_set.empty()) {
        return;
    }

    std::shared_ptr<NetPacket> packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto buffer = packet->GetData();

    SendOperation send_operation;
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
        // select destination address from connection (future: candidate path probing)
        packet->SetAddress((*iter)->AcquireSendAddress());
        packet->SetSocket((*iter)->GetSocket());  // client connection will always -1

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
            case SendOperation::kSendAgainImmediately:  // do nothing, send again immediately
            default:
                break;
        }
    }
}

bool Worker::SendImmediate(std::shared_ptr<common::IBuffer> buffer, const common::Address& addr, int32_t socket) {
    if (!buffer || buffer->GetDataLength() == 0) {
        common::LOG_WARN("SendImmediate: invalid buffer or empty data");
        return false;
    }

    std::shared_ptr<NetPacket> packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    packet->SetData(buffer);
    packet->SetAddress(addr);
    packet->SetSocket(socket);

    if (!sender_->Send(packet)) {
        common::LOG_ERROR("SendImmediate: udp send failed");
        return false;
    }

    common::LOG_DEBUG("SendImmediate: sent %zu bytes to %s", buffer->GetDataLength(), addr.AsString().c_str());
    return true;
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
    common::LOG_DEBUG("Worker::HandleHandshakeDone called, connecting_set size=%zu", connecting_set_.size());
    if (connecting_set_.find(conn) != connecting_set_.end()) {
        common::LOG_DEBUG("Connection found in connecting_set, moving to conn_map");
        connecting_set_.erase(conn);
        conn_map_[conn->GetConnectionIDHash()] = conn;
        common::LOG_DEBUG(
            "Added to conn_map with hash=%llu, conn_map size=%zu", conn->GetConnectionIDHash(), conn_map_.size());
        connection_handler_(conn, ConnectionOperation::kConnectionCreate, 0, "");
    } else {
        common::LOG_WARN("Connection NOT found in connecting_set! Cannot add to conn_map");
    }
}

void Worker::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    GetWriteActiveSendConnectionSet().insert(conn);
    common::LOG_DEBUG("HandleActiveSendConnection, is current:%d", active_send_connection_set_1_is_current_ ? 1 : 2);
    do_send_ = true;
    // Use saved event_loop_ if available, otherwise fallback to thread-local EventLoop
    if (event_loop_) {
        event_loop_->Wakeup();
    }
}

void Worker::HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    common::LOG_DEBUG("HandleConnectionClose. cid:%llu", conn->GetConnectionIDHash());
    // Remove all CIDs associated with this connection
    // A connection may have multiple CIDs: Initial DCID + NEW_CONNECTION_IDs
    auto cid_hashes = conn->GetAllLocalCIDHashes();
    for (uint64_t hash : cid_hashes) {
        auto it = conn_map_.find(hash);
        if (it != conn_map_.end()) {
            common::LOG_DEBUG("Removing CID %llu from conn_map during connection close", hash);
            conn_map_.erase(it);
        }
    }

    // Also remove from connecting_set if still there
    connecting_set_.erase(conn);

    connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
}

std::unordered_set<std::shared_ptr<IConnection>>& Worker::GetReadActiveSendConnectionSet() {
    return active_send_connection_set_1_is_current_ ? active_send_connection_set_1_ : active_send_connection_set_2_;
}

std::unordered_set<std::shared_ptr<IConnection>>& Worker::GetWriteActiveSendConnectionSet() {
    return active_send_connection_set_1_is_current_ ? active_send_connection_set_2_ : active_send_connection_set_1_;
}

void Worker::SwitchActiveSendConnectionSet() {
    // Merge Write set into current Read set, then switch to use the Write set as new Read set
    if (active_send_connection_set_1_is_current_) {
        // current=true: Read=set1, Write=set2
        // Move unfinished connections from Read(set1) to Write(set2)
        active_send_connection_set_2_.insert(
            active_send_connection_set_1_.begin(), active_send_connection_set_1_.end());
        active_send_connection_set_1_.clear();
        active_send_connection_set_1_is_current_ = false;
        // Now Read=set2, Write=set1
    } else {
        // current=false: Read=set2, Write=set1
        // Move unfinished connections from Read(set2) to Write(set1)
        active_send_connection_set_1_.insert(
            active_send_connection_set_2_.begin(), active_send_connection_set_2_.end());
        active_send_connection_set_2_.clear();
        active_send_connection_set_1_is_current_ = true;
        // Now Read=set1, Write=set2
    }
}

}  // namespace quic
}  // namespace quicx