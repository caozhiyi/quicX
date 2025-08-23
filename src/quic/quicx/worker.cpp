#include "common/log/log.h"
#include "quic/quicx/worker.h"
#include "quic/udp/if_sender.h"
#include "common/timer/timer.h"
#include "quic/common/version.h"
#include "quic/packet/init_packet.h"

namespace quicx {
namespace quic {

Worker::Worker(const QuicConfig& config, 
        std::shared_ptr<TLSCtx> ctx,
        std::shared_ptr<ISender> sender,
        const QuicTransportParams& params,
        connection_state_callback connection_handler):
    ctx_(ctx),
    params_(params),
    sender_(sender),
    connection_handler_(connection_handler) {
    time_ = common::MakeTimer();

    ecn_enabled_ = config.enable_ecn_;
}

Worker::~Worker() {

}

void Worker::Init(std::shared_ptr<IConnectionIDNotify> connection_id_notify) {
    connection_id_notify_ = connection_id_notify;
    Start();
}

void Worker::Destroy() {
    Thread::Stop();
}

void Worker::Weakup() {
    packet_queue_.Push(PacketInfo());
}

void Worker::Join() {
    Thread::Join();
}

std::thread::id Worker::GetCurrentThreadId() {
    if (pthread_) {
        return pthread_->get_id();
    }
    throw ("thread not started");
}

void Worker::HandlePacket(PacketInfo& packet_info) {
    packet_queue_.Emplace(std::move(packet_info));
}

void Worker::Run() {
    while (!Thread::IsStop()) {
        ProcessRecv();
        ProcessSend();
        ProcessTimer();
        ProcessTask();
    }
}

void Worker::ProcessTimer() {
    time_->TimerRun();
}

void Worker::ProcessSend() {
    static thread_local uint8_t buf[1500] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));

    std::shared_ptr<NetPacket> packet = std::make_shared<NetPacket>();
    SendOperation send_operation;
    for (auto iter = active_send_connection_set_.begin(); iter != active_send_connection_set_.end();) {
        if (!(*iter)->GenerateSendData(buffer, send_operation)) {
            common::LOG_ERROR("generate send data failed.");
            iter = active_send_connection_set_.erase(iter);
            continue;
        }

        if (buffer->GetDataLength() == 0) {
            common::LOG_WARN("generate send data length is 0.");
            iter = active_send_connection_set_.erase(iter);
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
                iter = active_send_connection_set_.erase(iter);
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

void Worker::ProcessRecv() {
    int32_t min_time = time_->MinTime();
    if (min_time < 0) {
        min_time = 200; // 200ms
    }

    PacketInfo packet_info;
    if (packet_queue_.TryPop(packet_info, std::chrono::milliseconds(min_time))) {
        if (packet_info.net_packet_ && packet_info.net_packet_->GetTime() > 0 && !packet_info.packets_.empty()) {
            InnerHandlePacket(packet_info);
        }
    }
}

void Worker::ProcessTask() {
    std::function<void()> task;
    while (queue_.TryPop(task)) {
        task();
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
        notify->AddConnectionID(cid);
    }
}

void Worker::HandleRetireConnectionId(ConnectionID& cid) {
    conn_map_.erase(cid.Hash());
    if (auto notify = connection_id_notify_.lock()) {
        notify->RetireConnectionID(cid);
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
    active_send_connection_set_.insert(conn);
    do_send_ = true;
    Weakup();
}

void Worker::HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    conn_map_.erase(conn->GetConnectionIDHash());
    if (error > 0) {
        connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
    } else {
        connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
    }
}

}
}