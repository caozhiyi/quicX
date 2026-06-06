#include <sstream>
#include <thread>

#include "common/log/log.h"
#include "common/log/log_context.h"
#include "common/qlog/qlog.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

#include "quic/common/version.h"
#include "quic/common/constants.h"
#include "quic/config.h"
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

    // PERF (sendmmsg batch path): per-thread reusable buffer of NetPackets
    // collected during one drain round. We push packets here instead of
    // calling sender_->Send() per packet, then issue a single
    // sender_->SendBatch() (one sendmmsg(2) syscall on Linux) at the end of
    // each connection's drain. Capacity matches kMaxPacketsPerRound below;
    // because the vector is thread_local and we only clear() (never shrink),
    // steady state is zero allocation.
    thread_local std::vector<std::shared_ptr<NetPacket>> tx_batch;
    if (tx_batch.capacity() < static_cast<size_t>(kMaxPacketsPerRound)) {
        tx_batch.reserve(kMaxPacketsPerRound);
    }

    // Iterate through active connections and try to send data
    for (auto iter = active_connections.begin(); iter != active_connections.end();) {
        bool has_more_data = false;
        auto conn = *iter;
        common::LogTagGuard guard("conn:" + std::to_string(conn->GetConnectionIDHash()));

        // Try to send data using the new high-level interface.
        // TrySend() handles all packet building and sending internally.
        // Limit packets per round to allow event loop to process incoming data.
        // This is critical for flow control: we need to receive MAX_STREAM_DATA
        // frames from the peer to unblock flow control.
        //
        // PERF tuning history (200 MB loopback file_transfer, macOS arm64):
        // -- Pre recv-batching (single recvfrom per wakeup) --
        //     32   -> ~6.6 MB/s (yields too often, RTT inflation)
        //     64   -> ~8.1 MB/s
        //    100   -> ~5.0 MB/s (ack-feedback starved)
        //    128   -> ~6.6 MB/s (still starves)
        //    256   -> ~5.4 MB/s (worst)
        // -- Post recv-batching (recvmmsg/drain up to 64 per wakeup) --
        //     64   -> ~34.0 MB/s (old default)
        //    128   -> ~36.6 MB/s (chosen — +7.6%, ack feedback keeps up)
        //    256   -> ~30.4 MB/s (-10.6%; ack-feedback re-starves)
        //   1024   -> ~31.5 MB/s (-7.4%)
        // 128 is the new sweet spot now that recv-side drains in batches
        // — ack feedback can match a doubled send window per round, but
        // 256+ pushes ack processing past its budget per loop iteration.
        // The cap is centralized in quic/config.h::kMaxPacketsPerRound so
        // benchmark sweeps only touch one place.
        int packets_sent = 0;
        
        // Install the per-round batch sink so SendBuffer() inside TrySend()
        // appends NetPackets here instead of calling sender_->Send() per
        // packet. We always clear the sink first so a previous iteration's
        // residue (cleared after flush below, but defensive) cannot leak.
        tx_batch.clear();
        conn->SetSendSink(&tx_batch);

        while (packets_sent < kMaxPacketsPerRound && conn->TrySend()) {
            has_more_data = true;
            packets_sent++;
        }

        // Detach the sink BEFORE flushing so any sender_->Send() fallback
        // inside SendBatch (e.g. cache miss on first round) doesn't
        // re-enter SendBuffer's sink branch.
        conn->SetSendSink(nullptr);

        // Single sendmmsg(2) over the whole drain. The fast path is one
        // syscall total; the cache-miss / fault-injection / mixed-socket
        // fallbacks inside UdpSender::SendBatch degrade gracefully to N
        // sendto()s with no semantic change.
        if (!tx_batch.empty()) {
            const uint32_t sent = sender_->SendBatch(tx_batch);
            if (sent < tx_batch.size()) {
                // Already logged inside SendBatch with detail; this is a
                // cheap counter-side signal so a steady stream of short-
                // writes shows up in conn-level logs too.
                LOG_DEBUG("ProcessSend: SendBatch sent %u/%zu",
                          sent, tx_batch.size());
            }
            // Drop refs immediately so the underlying buffers can be
            // recycled by their pool before the next connection's drain.
            tx_batch.clear();
        }

        // PERF DIAG: distribution of "packets emitted in a single per-conn
        // ProcessSend pass". If this is heavily biased toward 1, the worker
        // is being woken once per packet (= sendto-bound). If it's saturating
        // at kMaxPacketsPerRound, the cap *is* the bottleneck and raising
        // kMaxPacketsPerRound (in quic/config.h) would help. We sample
        // whether we sent zero packets too — that means we entered
        // ProcessSend without anything to do.
        common::Metrics::HistogramObserve(
            common::MetricsStd::DiagPktPerIterHist,
            static_cast<uint64_t>(packets_sent));
        
        // If we hit the limit, keep connection in active set for next round
        if (packets_sent >= kMaxPacketsPerRound) {
            has_more_data = true;
        }

        // If connection has no more data to send, remove from active set
        if (!has_more_data) {
            LOG_DEBUG("Worker::ProcessSend: connection has no more data, removing from active set");
            iter = active_connections.erase(iter);
        } else {
            ++iter;
        }
    }
}

bool Worker::SendImmediate(std::shared_ptr<common::IBuffer> buffer, const common::Address& addr, int32_t socket) {
    if (!buffer || buffer->GetDataLength() == 0) {
        LOG_WARN("SendImmediate: invalid buffer or empty data");
        return false;
    }

    std::shared_ptr<NetPacket> packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    packet->SetData(buffer);
    packet->SetAddress(addr);
    packet->SetSocket(socket);

    if (!sender_->Send(packet)) {
        LOG_ERROR("SendImmediate: udp send failed");
        return false;
    }

    LOG_DEBUG("SendImmediate: sent %zu bytes to %s", buffer->GetDataLength(), addr.AsString().c_str());
    return true;
}

bool Worker::InitPacketCheck(std::shared_ptr<IPacket> packet, uint32_t datagram_size) {
    if (packet->GetHeader()->GetPacketType() != PacketType::kInitialPacketType) {
        LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    // RFC 9000 §14.1: A server MUST discard an Initial packet that is carried
    // in a UDP datagram with a payload that is smaller than the smallest
    // maximum datagram size of kMinInitialPacketSize (=1200) bytes.
    // NOTE: We check the original UDP datagram size, not the decoded packet body size.
    if (datagram_size < kMinInitialPacketSize) {
        LOG_ERROR("init packet datagram too small. datagram_size:%d", datagram_size);
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
    bool was_present = conn_map_.count(cid.Hash()) > 0;
    void* prev_ptr = nullptr;
    if (was_present) {
        prev_ptr = (void*)conn_map_[cid.Hash()].get();
    }
    conn_map_[cid.Hash()] = conn;
    LOG_INFO("[DISPATCH-TRACE] add_cid cid_hash=%llu conn=%p was_present=%d prev=%p conn_map=%zu",
        cid.Hash(), (void*)conn.get(), was_present ? 1 : 0, prev_ptr, conn_map_.size());
    LOG_DEBUG("add connection id to client worker. cid:%llu", cid.Hash());
    if (auto notify = connection_id_notify_.lock()) {
        notify->AddConnectionID(cid, GetWorkerId());
    }
}

void Worker::HandleRetireConnectionId(ConnectionID& cid) {
    size_t erased = conn_map_.erase(cid.Hash());
    LOG_INFO("[DISPATCH-TRACE] retire_cid cid_hash=%llu erased=%zu conn_map=%zu",
        cid.Hash(), erased, conn_map_.size());
    if (auto notify = connection_id_notify_.lock()) {
        notify->RetireConnectionID(cid, GetWorkerId());
    }
}

void Worker::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    LOG_DEBUG("Worker::HandleHandshakeDone called, connecting_set size=%zu", connecting_set_.size());
    bool in_connecting = connecting_set_.find(conn) != connecting_set_.end();
    LOG_INFO("[DISPATCH-TRACE] handshake_done conn=%p scid_hash=%llu in_connecting=%d "
             "conn_map=%zu connecting_set=%zu",
        (void*)conn.get(), conn->GetConnectionIDHash(), in_connecting ? 1 : 0,
        conn_map_.size(), connecting_set_.size());
    if (in_connecting) {
        LOG_DEBUG("Connection found in connecting_set, moving to conn_map");
        connecting_set_.erase(conn);
        conn_map_[conn->GetConnectionIDHash()] = conn;
        LOG_DEBUG(
            "Added to conn_map with hash=%llu, conn_map size=%zu", conn->GetConnectionIDHash(), conn_map_.size());

        // Check if 0-RTT early data write key is available: if so, this is an early connection
        const bool early = conn->HasEarlyDataWriteKey();
        ConnectionOperation op = early ? ConnectionOperation::kEarlyConnection
                                       : ConnectionOperation::kConnectionCreate;
        connection_handler_(conn, op, 0, "");
    } else {
        // Connection already moved out of connecting_set (e.g. early connection triggered earlier).
        // If this is the full handshake completion after an early connection, notify kConnectionCreate.
        if (conn_map_.count(conn->GetConnectionIDHash())) {
            LOG_DEBUG("Post-early-connection handshake done, notifying kConnectionCreate");
            connection_handler_(conn, ConnectionOperation::kConnectionCreate, 0, "");
        } else {
            LOG_WARN("Connection NOT found in connecting_set or conn_map! Cannot handle handshake done");
        }
    }
}

void Worker::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    // Add to write buffer (safe during ProcessSend execution)
    active_send_connections_.Add(conn);
    LOG_DEBUG("HandleActiveSendConnection: added connection to write buffer");
    do_send_ = true;
    // Use saved event_loop_ if available, otherwise fallback to thread-local EventLoop
    auto loop = event_loop_.lock();
    if (loop) {
        loop->Wakeup();
    }
}

void Worker::HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    LOG_DEBUG("HandleConnectionClose. cid:%llu", conn->GetConnectionIDHash());
    // Remove all CIDs associated with this connection
    // A connection may have multiple CIDs: Initial DCID + NEW_CONNECTION_IDs
    auto cid_hashes = conn->GetAllLocalCIDHashes();
    size_t local_removed = 0;
    for (uint64_t hash : cid_hashes) {
        auto it = conn_map_.find(hash);
        if (it != conn_map_.end()) {
            LOG_DEBUG("Removing CID %llu from conn_map during connection close", hash);
            conn_map_.erase(it);
            ++local_removed;
        }
    }

    // Fallback sweep: conn_map_ may also contain entries keyed by CIDs that
    // were *not* generated by our local CID manager — most notably the Initial
    // DCID the peer picked and that ServerWorker::InnerHandlePacket inserts
    // directly as conn_map_[dst_cid.Hash()]. Those keys are not returned by
    // GetAllLocalCIDHashes(), so without this pass we would leak a shared_ptr
    // reference to the connection forever, preventing ~BaseConnection() from
    // ever running. This is the root cause of the P4 per-connection RSS
    // residue (~120 KB) observed in profile_rss_lifecycle.
    size_t orphan_removed = 0;
    for (auto it = conn_map_.begin(); it != conn_map_.end();) {
        if (it->second.get() == conn.get()) {
            LOG_DEBUG("Removing orphan CID %llu (not in local CID manager) from conn_map", it->first);
            it = conn_map_.erase(it);
            ++orphan_removed;
        } else {
            ++it;
        }
    }

    // Also remove from connecting_set if still there
    bool was_connecting = connecting_set_.erase(conn) > 0;

    LOG_INFO("[DISPATCH-TRACE] conn_close conn=%p scid_hash=%llu err=%llu reason=\"%s\" "
             "local_removed=%zu orphan_removed=%zu was_connecting=%d "
             "conn_map=%zu connecting_set=%zu",
        (void*)conn.get(), conn->GetConnectionIDHash(), (unsigned long long)error, reason.c_str(),
        local_removed, orphan_removed, was_connecting ? 1 : 0,
        conn_map_.size(), connecting_set_.size());

    connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
}

void Worker::Shutdown() {
    // Drop the double-buffered active-send set first: each entry holds a
    // shared_ptr<IConnection>, so until it is emptied the connection keeps
    // BaseConnection::event_loop_ alive (which in turn keeps this Worker
    // alive via the fixed-process closure).
    active_send_connections_.Clear();

    // Drop all connection-id-keyed references. Note that conn_map_ may
    // contain multiple keys pointing at the same connection (the Initial
    // DCID plus any NEW_CONNECTION_IDs), and connecting_set_ holds in-
    // flight handshake connections that never reached conn_map_.
    conn_map_.clear();
    connecting_set_.clear();

    // Drop any application-layer callback so its captured state (e.g.
    // Http3::Client's conn_map_ of ClientConnection) is released too.
    connection_handler_ = nullptr;
}

}  // namespace quic
}  // namespace quicx