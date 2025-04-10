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
}

UdpSender::UdpSender(uint64_t sock):
    sock_(sock) {

}

bool UdpSender::Send(std::shared_ptr<INetPacket>& pkt) {
    auto buffer= pkt->GetData();
    auto span = buffer->GetReadSpan();
    auto ret = common::SendTo(sock_, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    common::LOG_DEBUG("send packet to: %s, len: %d", pkt->GetAddress().AsString().c_str(), span.GetLength());
    return ret.errno_ == 0;
}

}
}
