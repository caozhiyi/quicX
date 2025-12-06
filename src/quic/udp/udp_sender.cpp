#include "quic/udp/udp_sender.h"
#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace quic {

UdpSender::UdpSender():
    sock_(-1) {}

UdpSender::UdpSender(int32_t sockfd):
    sock_(sockfd) {}

bool UdpSender::Send(std::shared_ptr<NetPacket>& pkt) {
    auto buffer = pkt->GetData();
    auto span = buffer->GetReadableSpan();
    auto sock = pkt->GetSocket() > 0 ? pkt->GetSocket() : sock_;
    if (sock <= 0) {
        common::LOG_ERROR(
            "send packet to: %s, len: %d, sock: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), sock);
        return false;
    }
    auto ret = common::SendTo(sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    if (ret.errno_ != 0) {
        common::LOG_ERROR(
            "send packet to: %s, len: %d, err: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), ret.errno_);
        common::Metrics::CounterInc(common::MetricsStd::UdpSendErrors);
        return false;
    }
    common::LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());

    // Metrics: UDP packet sent successfully
    common::Metrics::CounterInc(common::MetricsStd::UdpPacketsTx);
    common::Metrics::CounterInc(common::MetricsStd::UdpBytesTx, span.GetLength());

    return true;
}

}  // namespace quic
}  // namespace quicx
