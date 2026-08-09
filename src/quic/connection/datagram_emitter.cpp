#include "quic/connection/datagram_emitter.h"

#include <utility>

#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/qlog/qlog.h"
#include "common/util/time.h"

namespace quicx {
namespace quic {

DatagramEmitter::DatagramEmitter(
    SendControl& send_control, AddressProvider addr_provider, std::shared_ptr<common::QlogTrace> qlog_trace)
    : send_control_(send_control), addr_provider_(std::move(addr_provider)), qlog_trace_(std::move(qlog_trace)) {}

// ==================== Scope ====================

DatagramEmitter::Scope::~Scope() {
    if (owner_ && !committed_) {
        owner_->AbortDatagram();
    }
}

DatagramEmitter::Scope::Scope(Scope&& other) noexcept: owner_(other.owner_), committed_(other.committed_) {
    other.owner_ = nullptr;
}

DatagramEmitter::Scope& DatagramEmitter::Scope::operator=(Scope&& other) noexcept {
    if (this != &other) {
        if (owner_ && !committed_) {
            owner_->AbortDatagram();
        }
        owner_ = other.owner_;
        committed_ = other.committed_;
        other.owner_ = nullptr;
    }
    return *this;
}

bool DatagramEmitter::Scope::Commit(std::shared_ptr<common::IBuffer> buffer, bool bypass_batch) {
    if (!owner_) {
        LOG_ERROR("DatagramEmitter::Scope::Commit: moved-from scope");
        return false;
    }
    if (committed_) {
        LOG_ERROR("DatagramEmitter::Scope::Commit: double commit");
        return false;
    }
    committed_ = true;
    return owner_->Emit(std::move(buffer), bypass_batch);
}

// ==================== DatagramEmitter ====================

DatagramEmitter::Scope DatagramEmitter::Open() {
    send_control_.BeginSendDatagram(next_datagram_id_++);
    return Scope(this);
}

void DatagramEmitter::AbortDatagram() {
    // Discard the bracket. Nothing was shipped, so no datagrams_sent event:
    // qvis would otherwise render a datagram that never left the host.
    (void)send_control_.EndSendDatagram();
}

void DatagramEmitter::CloseBracketAndLog() {
    // Drain the accumulator BEFORE handing bytes off: the batch-sink path moves
    // ownership of the buffer and returns immediately, but the per-packet
    // OnPacketSend callbacks that populate the accumulator have already fired
    // during packet build.
    auto summary = send_control_.EndSendDatagram();

    // Only emit when qlog is on and at least one packet was recorded; a
    // zero-count datagram is benign, just skip it.
    if (qlog_trace_ && summary.packet_count > 0) {
        common::DatagramsSentData dg_data;
        dg_data.datagram_id = summary.datagram_id;
        dg_data.count = summary.packet_count;
        dg_data.raw_length = summary.raw_length;
        dg_data.packet_numbers = std::move(summary.packet_numbers);
        QLOG_DATAGRAMS_SENT(qlog_trace_, dg_data);
    }
}

bool DatagramEmitter::Emit(std::shared_ptr<common::IBuffer> buffer, bool bypass_batch) {
    // Close the qlog bracket first, unconditionally — even if the send below
    // fails, the datagram must not stay open.
    CloseBracketAndLog();

    if (!buffer || buffer->GetDataLength() == 0) {
        LOG_WARN("DatagramEmitter::Emit: empty buffer");
        common::Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
        return false;
    }

    if (!sender_) {
        LOG_ERROR("DatagramEmitter::Emit: no sender available");
        common::Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
        return false;
    }

    // The provider hands back the destination address with its sockaddr cache
    // already warmed (see AddressProvider docs and BaseConnection's ctor for
    // why the warm-up has to happen on the caller's long-lived storage).
    //
    // NB: the old SendImmediate path had no warm-up at all, which meant every
    // handshake packet and every immediate ACK degraded UdpSender::SendBatch's
    // fast-path probe to a per-packet sendto. Routing both egress modes through
    // here fixes that asymmetry.
    common::Address send_addr = addr_provider_();

    const int32_t send_sock = GetActiveSocket();

    auto packet = std::make_shared<NetPacket>();
    packet->SetData(buffer);
    packet->SetAddress(send_addr);
    packet->SetSocket(send_sock);

    // PERF (sendmmsg batch path): when Worker has installed a sink for this
    // drain round, queue the packet and return. The sendmmsg(2) syscall is
    // issued once at the end of the drain over the whole accumulated batch.
    // Order is preserved (push_back is FIFO) and nothing is buffered across
    // rounds — Worker flushes before returning from ProcessSend.
    //
    // bypass_batch skips this: handshake / immediate-ACK datagrams must hit the
    // wire now rather than wait for the flush.
    if (send_sink_ && !bypass_batch) {
        const uint32_t len = buffer->GetDataLength();
        send_sink_->push_back(std::move(packet));
        LOG_DEBUG("DatagramEmitter::Emit: queued %u bytes for batch, sock=%d", len, send_sock);
        return true;
    }

    if (!sender_->Send(packet)) {
        LOG_ERROR("DatagramEmitter::Emit: sender_->Send() failed, sock=%d", send_sock);
        common::Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
        return false;
    }

    LOG_DEBUG("DatagramEmitter::Emit: sent %u bytes, sock=%d, bypass_batch=%d", buffer->GetDataLength(), send_sock,
        static_cast<int>(bypass_batch));
    return true;
}

int32_t DatagramEmitter::SwitchToProbeSocket() {
    const int32_t retired = sockfd_;
    const int32_t probe = probe_sockfd_;
    sockfd_ = probe_sockfd_;
    probe_sockfd_ = 0;
    return retired;
}

}  // namespace quic
}  // namespace quicx
