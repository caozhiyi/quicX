#ifndef QUIC_CONNECTION_CONTROLER_RECV_CONTROL
#define QUIC_CONNECTION_CONTROLER_RECV_CONTROL

#include <set>
#include <functional>
#include "quic/packet/type.h"
#include "common/timer/if_timer.h"
#include "quic/packet/if_packet.h"

namespace quicx {
namespace quic {

// controller of receiver. 

/*
1. max_ack_delay 时间内必须回复一个ack make a timer.
2. 立即确认所有Initial和Handshake触发包 以及在通告的max_ack_delay内确认所有0-RTT和1-RTT触发包，以下情况除外：在握手确认之前，终端可能没有可用的秘钥在收到Handshake、0-RTT或1-RTT包时对其解密
3. 不得（MUST NOT）发送非ACK触发包来响应非ACK触发包
4. 为了帮助发送方进行丢包检测，终端应该（SHOULD）在接收到ACK触发包时立即生成并发送一个ACK帧：
   当收到的数据包的编号小于另一个已收到的ACK触发包时；
   当数据包的编号大于已接收到的最高编号的ACK触发包，并且编号不连续时
5. 接收方应该（SHOULD）在收到至少两个ACK触发包后才发送一个ACK帧
6. 接收方在每个ACK帧中都应该（SHOULD）包含一个ACK Range，该Range包含最大接收包号
*/

typedef std::function<void()> ActiveSendCB;

class RecvControl {
public:
    RecvControl(std::shared_ptr<common::ITimer> timer);
    ~RecvControl() {}

    void OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet);
    std::shared_ptr<IFrame> MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns);

    void SetActiveSendCB(ActiveSendCB cb) { active_send_cb_ = cb; }

private:
    uint64_t pkt_num_largest_recvd_[PNS_NUMBER];
    uint64_t largest_recv_time_[PNS_NUMBER];
    std::set<uint64_t> wait_ack_packet_numbers_[PNS_NUMBER];
    
    bool set_timer_;
    std::shared_ptr<common::ITimer> timer_;

    common::TimerTask timer_task_;
    ActiveSendCB active_send_cb_;
};

}
}

#endif