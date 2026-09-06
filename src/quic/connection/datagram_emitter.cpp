#include <quicx/common/metrics.h>
#include <utility>

#include "common/log/log.h"
#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"
#include "common/util/time.h"

#include "quic/common/constants.h"
#include "quic/connection/datagram_emitter.h"

namespace quicx {
namespace quic {

namespace {
// Trailing bytes used to top a datagram up to the RFC 9000 §14.1 floor. Zero
// bytes parse as neither a valid long nor short header, so a receiver discards
// them — which §14.1/§12.2 explicitly permit for reaching the floor. Const
// zero-initialised, so this costs no space in the binary (it lives in .bss).
const uint8_t kPaddingZeros[kMinInitialPacketSize] = {0};
}  // namespace

DatagramEmitter::DatagramEmitter(
    SendControl& send_control, AddressProvider addr_provider, std::shared_ptr<common::QlogTrace> qlog_trace):
    send_control_(send_control),
    addr_provider_(std::move(addr_provider)),
    qlog_trace_(std::move(qlog_trace)) {}

// ==================== Scope ====================

DatagramEmitter::Scope::~Scope() {
    if (owner_ && !committed_) {
        owner_->AbortDatagram();
    }
}

DatagramEmitter::Scope::Scope(Scope&& other) noexcept:
    owner_(other.owner_),
    committed_(other.committed_) {
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
    datagram_carries_initial_ = false;
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
    // RFC 9000 §14.1: datagrams carrying Initial packets must reach the 1200 B
    // floor. Do it here, against the datagram's *measured* size, instead of
    // predicting that size while the packet is being built.
    //
    // Prediction is what the in-builder padding has to do, and it has been wrong
    // twice on this codebase: the coalescing path budgeted a 29 B envelope while
    // this server's long headers actually cost ~68 B (it issues 20-byte CIDs),
    // and the retransmit path subtracted an envelope that InitPacket::Encode()
    // had already counted. Both shipped datagrams tens of bytes short of 1200,
    // which peers enforcing §14.1 (picoquic) discard as "Server initial too
    // short" — the client then retransmits on an exponential backoff until the
    // 30 s handshake watchdog fires. Measured here, the gap cannot be misjudged.
    //
    // Zero-filled trailing bytes are legal padding for this purpose: RFC 9000
    // §14.1 allows reaching the floor "by coalescing the Initial packet" and
    // §12.2 notes the trailing packet "can even be ... invalid, which a receiver
    // will discard". This mirrors aioquic's _flush_current_datagram().
    const bool needs_padding = datagram_carries_initial_;
    datagram_carries_initial_ = false;
    if (needs_padding && buffer && buffer->GetDataLength() > 0) {
        const uint32_t current = buffer->GetDataLength();
        if (current < kMinInitialPacketSize) {
            // Pad all the way or not at all: a datagram padded only part way to
            // 1200 is still under the floor, so peers that check it drop it
            // anyway — those bytes would cost §8.1 budget and buy nothing.
            const bool affordable = amp_budget_query_ ? amp_budget_query_(kMinInitialPacketSize) : true;
            if (affordable) {
                buffer->Write(kPaddingZeros, kMinInitialPacketSize - current);
            } else {
                LOG_DEBUG(
                    "DatagramEmitter::Emit: skipping %u B of §14.1 padding, "
                    "RFC 9000 8.1 budget cannot cover the padded datagram",
                    kMinInitialPacketSize - current);
            }
        }
    }

    // RFC 9000 §8.1: enforce the 3x anti-amplification budget before anything
    // else. Checked ahead of CloseBracketAndLog() so a dropped datagram is not
    // reported to qlog as sent, while the bracket is still closed on this path
    // (otherwise the next datagram would inherit a stale datagram_id).
    if (amp_budget_check_ && buffer && buffer->GetDataLength() > 0) {
        if (!amp_budget_check_(buffer->GetDataLength())) {
            LOG_WARN("DatagramEmitter::Emit: blocked %u bytes by anti-amplification limit", buffer->GetDataLength());
            (void)send_control_.EndSendDatagram();
            return false;
        }
    }

    // Close the qlog bracket first, unconditionally — even if the send below
    // fails, the datagram must not stay open.
    CloseBracketAndLog();

    if (!buffer || buffer->GetDataLength() == 0) {
        LOG_WARN("DatagramEmitter::Emit: empty buffer");
        Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
        return false;
    }

    if (!sender_) {
        LOG_ERROR("DatagramEmitter::Emit: no sender available");
        Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
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

    const common::SocketHandle send_sock = GetActiveSocket();

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
        LOG_DEBUG("DatagramEmitter::Emit: queued %u bytes for batch, sock=%d", len, send_sock.fd);
        return true;
    }

    if (!sender_->Send(packet)) {
        LOG_ERROR("DatagramEmitter::Emit: sender_->Send() failed, sock=%d", send_sock.fd);
        Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
        return false;
    }

    LOG_DEBUG("DatagramEmitter::Emit: sent %u bytes, sock=%d, bypass_batch=%d", buffer->GetDataLength(), send_sock.fd,
        static_cast<int>(bypass_batch));
    return true;
}

common::SocketHandle DatagramEmitter::SwitchToProbeSocket() {
    const common::SocketHandle retired = sock_;
    sock_ = probe_sock_;
    probe_sock_ = common::SocketHandle(0, 0);
    return retired;
}

}  // namespace quic
}  // namespace quicx
