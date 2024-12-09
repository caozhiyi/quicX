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
    
    _sock = ret._return_value;
}

UdpSender::UdpSender(uint64_t sock):
    _sock(sock) {

}

bool UdpSender::Send(std::shared_ptr<INetPacket>& pkt) {
    auto buffer= pkt->GetData();
    auto span = buffer->GetReadSpan();
    auto ret = common::SendTo(_sock, (const char*)span.GetStart(), span.GetLength(), 0, pkt->GetAddress());
    
    return ret.errno_ == 0;
}

}
}
