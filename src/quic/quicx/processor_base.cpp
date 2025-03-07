#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/common/constants.h"
#include "common/network/address.h"
#include "common/timer/timer_task.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/quicx/processor_base.h"
#include "quic/packet/header/long_header.h"
#include "quic/quicx/connection_transfor.h"
#include "quic/packet/header/short_header.h"
#include "quic/connection/connection_server.h"
#include "quic/packet/version_negotiation_packet.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

thread_local std::shared_ptr<common::ITimer> ProcessorBase::time_ = common::MakeTimer();

ProcessorBase::ProcessorBase(std::shared_ptr<TLSCtx> ctx,
    const QuicTransportParams& params,
    connection_state_callback connection_handler):
    do_send_(false),
    ctx_(ctx),
    params_(params),
    connection_handler_(connection_handler) {
    alloter_ = std::make_shared<common::BlockMemoryPool>(1500, 5); // todo add to config
}

ProcessorBase::~ProcessorBase() {

}

void ProcessorBase::Process() {
    int64_t waittime_ = time_->MinTime();
    waittime_ = waittime_ < 0 ? 100000 : waittime_;
    // try to receive data from network
    ProcessRecv(waittime_);

    // send all data to network
    if (do_send_) {
        ProcessSend();
        do_send_ = active_send_connection_set_.empty();
    }

    // check timer and do timer task
    ProcessTimer();
}

void ProcessorBase::AddReceiver(uint64_t socket_fd) {
    receiver_->AddReceiver(socket_fd);
}

void ProcessorBase::AddReceiver(const std::string& ip, uint16_t port) {
    receiver_->AddReceiver(ip, port);
}

void ProcessorBase::AddTimer(uint32_t interval_ms, timer_callback cb) {
    common::TimerTask task(cb);
    time_->AddTimer(task, interval_ms);
}

void ProcessorBase::ProcessRecv(uint32_t timeout_ms) {
    uint8_t recv_buf[kMaxV4PacketSize] = {0};
    std::shared_ptr<INetPacket> packet = std::make_shared<INetPacket>();
    auto buffer = std::make_shared<common::Buffer>(recv_buf, sizeof(recv_buf));
    packet->SetData(buffer);

    receiver_->TryRecv(packet, timeout_ms);

    // if wakeup by timer, there may be no packet.
    if (packet->GetData()->GetDataLength() > 0) {
        HandlePacket(packet);
    }
}

void ProcessorBase::ProcessTimer() {
    time_->TimerRun();
}

void ProcessorBase::ProcessSend() {
    static thread_local uint8_t buf[1500] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));

    std::shared_ptr<INetPacket> packet = std::make_shared<INetPacket>();
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

void ProcessorBase::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    if (connecting_set_.find(conn) != connecting_set_.end()) {
        connecting_set_.erase(conn);
        conn_map_[conn->GetConnectionIDHash()] = conn;
        connection_handler_(conn, ConnectionOperation::kConnectionCreate, 0, "");
    }
}

void ProcessorBase::HandleActiveSendConnection(std::shared_ptr<IConnection> conn) {
    active_send_connection_set_.insert(conn);
    do_send_ = true;
    receiver_->Wakeup();
}

void ProcessorBase::HandleAddConnectionId(uint64_t cid_hash, std::shared_ptr<IConnection> conn) {
    conn_map_[cid_hash] = conn;
}

void ProcessorBase::HandleRetireConnectionId(uint64_t cid_hash) {
    conn_map_.erase(cid_hash);
}

void ProcessorBase::HandleConnectionClose(std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    conn_map_.erase(conn->GetConnectionIDHash());
    if (error > 0) {
        connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
    } else {
        connection_handler_(conn, ConnectionOperation::kConnectionClose, error, reason);
    }
}

void ProcessorBase::TransferConnection(uint64_t cid_hash, std::shared_ptr<IConnection>& conn) {
    conn->SetTimer(ProcessorBase::time_);
    conn->SetActiveConnectionCB(std::bind(&ProcessorBase::HandleActiveSendConnection, this, std::placeholders::_1));
    conn->SetHandshakeDoneCB(std::bind(&ProcessorBase::HandleHandshakeDone, this, std::placeholders::_1));
    conn->SetAddConnectionIdCB(std::bind(&ProcessorBase::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2));
    conn->SetRetireConnectionIdCB(std::bind(&ProcessorBase::HandleRetireConnectionId, this, std::placeholders::_1));
    conn->SetConnectionCloseCB(std::bind(&ProcessorBase::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    conn_map_[cid_hash] = conn;
}

bool ProcessorBase::InitPacketCheck(std::shared_ptr<IPacket> packet) {
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


bool ProcessorBase::DecodeNetPakcet(std::shared_ptr<INetPacket> net_packet,
    std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len) {
    if(!DecodePackets(net_packet->GetData(), packets)) {
        common::LOG_ERROR("decode packet failed");
        return false;
    }

    if (packets.empty()) {
        common::LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    auto first_packet_header = packets[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PacketHeaderType::kShortHeader) {
        auto short_header = dynamic_cast<ShortHeader*>(first_packet_header);
        len = short_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)short_header->GetDestinationConnectionId();

    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        len = long_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)long_header->GetDestinationConnectionId();
    }
    return true;
}

}
}