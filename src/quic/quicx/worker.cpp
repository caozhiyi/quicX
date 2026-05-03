#include <sstream>
#include <thread>

#include "common/log/log.h"
#include "common/log/log_context.h"
#include "common/qlog/qlog.h"

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
    event_loop_(event_loop) {
    ecn_enabled_ = config.enable_ecn_;
    enable_key_update_ = config.enable_key_update_;
    quic_version_ = config.quic_version_;
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
    ProcessSend();
}

void Worker::ProcessSend() {
    // Swap buffers: move unfinished connections from read to write buffer
    active_send_connections_.Swap();
    auto& active_connections = active_send_connections_.GetReadBuffer();

    if (active_connections.empty()) {
        return;
    }

    // Iterate through active connections and try to send data
    for (auto iter = active_connections.begin(); iter != active_connections.end();) {
        bool has_more_data = false;
        auto conn = *iter;
        common::LogTagGuard guard("conn:" + std::to_string(conn->GetConnectionIDHash()));

        // Try to send data using the new high-level interface
        // TrySend() handles all packet building and sending internally
        // Limit packets per round to allow event loop to process incoming data
        // This is critical for flow control: we need to receive MAX_STREAM_DATA
        // frames from the peer to unblock flow control
        constexpr int kMaxPacketsPerRound = 100;
        int packets_sent = 0;
        
        while (packets_sent < kMaxPacketsPerRound && conn->TrySend()) {
            has_more_data = true;
            packets_sent++;
        }
        
        // If we hit the limit, keep connection in active set for next round
        if (packets_sent >= kMaxPacketsPerRound) {
            has_more_data = true;
        }

        // If connection has no more data to send, remove from active set
        if (!has_more_data) {
            common::LOG_DEBUG("Worker::ProcessSend: connection has no more data, removing from active set");
            iter = active_connections.erase(iter);
        } else {
            ++iter;
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

bool Worker::InitPacketCheck(std::shared_ptr<IPacket> packet, uint32_t datagram_size) {
    if (packet->GetHeader()->GetPacketType() != PacketType::kInitialPacketType) {
        common::LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    // RFC 9000 §14.1: A server MUST discard an Initial packet that is carried
    // in a UDP datagram with a payload that is smaller than the smallest
    // maximum datagram size of 1200 bytes.
    // NOTE: We check the original UDP datagram size, not the decoded packet body size.
    if (datagram_size < 1200) {
        common::LOG_ERROR("init packet datagram too small. datagram_size:%d", datagram_size);
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

        // Check if 0-RTT early data write key is available: if so, this is an early connection
        auto early_crypto = conn->GetCryptographerForTest(kEarlyData);
        ConnectionOperation op = early_crypto ? ConnectionOperation::kEarlyConnection
                                              : ConnectionOperation::kConnectionCreate;
        connection_handler_(conn, op, 0, "");
    } else {
        // Connection already moved out of connecting_set (e.g. early connection triggered earlier).
        // If this is the full handshake completion after an early connection, notify kConnectionCreate.
        if (conn_map_.count(conn->GetConnectionIDHash())) {
            common::LOG_DEBUG("Post-early-connection handshake done, notifying kConnectionCreate");
            connection_handler_(conn, ConnectionOperation::kConnectionCreate, 0, "");
        } else {
            common::LOG_WARN("Connection NOT found in connecting_set or conn_map! Cannot handle handshake done");
        }
    }
}

void Worker::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    // Add to write buffer (safe during ProcessSend execution)
    active_send_connections_.Add(conn);
    common::LOG_DEBUG("HandleActiveSendConnection: added connection to write buffer");
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

}  // namespace quic
}  // namespace quicx