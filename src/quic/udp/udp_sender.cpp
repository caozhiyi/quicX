#include "quic/udp/udp_sender.h"
#include "common/network/io_handle.h"

namespace quicx {

bool UdpSender::DoSend(std::shared_ptr<UdpPacketOut> udp_packet) {
    auto buffer= udp_packet->GetData();
    auto span = buffer->GetReadSpan();
    auto ret = SendTo(udp_packet->GetOutSocket(), (const char*)span.GetStart(), span.GetLength(), 0, *(udp_packet->GetPeerAddress()));
    
    return ret.errno_ == 0;
}

}
