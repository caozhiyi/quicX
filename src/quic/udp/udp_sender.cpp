#include "common/log/log.h"
#include "quic/udp/udp_sender.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace quic {

UdpSender::UdpSender() {
    auto ret = common::UdpSocket();
    if (ret.errno_ != 0) {
        common::LOG_ERROR("create udp socket failed. err:%d", ret.errno_);
        abort();
        return;
    }
    
    sock_ = ret.return_value_;

    // marking deferred; controlled by config in master
}

UdpSender::UdpSender(int32_t sockfd):
    sock_(sockfd) {

}

bool UdpSender::Send(std::shared_ptr<NetPacket>& pkt) {
    auto buffer= pkt->GetData();
    auto span = buffer->GetReadSpan();
    auto sock = pkt->GetSocket() > 0 ? pkt->GetSocket() : sock_;
    auto ret = common::SendTo(sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    if (ret.errno_ != 0) {
        common::LOG_ERROR("send packet to: %s, len: %d, err: %d", pkt->GetAddress().AsString().c_str(), span.GetLength(), ret.errno_);
        return false;
    }
    common::LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());
    return true;
}

}
}
