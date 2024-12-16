#ifndef QUIC_CONNECTION_CONTROLER_SEND_MANAGER
#define QUIC_CONNECTION_CONTROLER_SEND_MANAGER

#include <list>
#include <memory>
#include <unordered_set>
#include "quic/packet/if_packet.h"
#include "quic/stream/if_stream.h"
#include "common/timer/if_timer.h"
#include "common/buffer/if_buffer.h"
#include "quic/packet/packet_number.h"
#include "quic/connection/if_packet_visitor.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/connection/controler/send_control.h"
#include "quic/connection/controler/flow_control.h"

namespace quicx {
namespace quic {

class SendManager {
public:
    SendManager(std::shared_ptr<common::ITimer> timer);
    ~SendManager();

    void AddFrame(std::shared_ptr<IFrame> frame);
    void AddActiveStream(std::shared_ptr<IStream> stream);

    bool GetSendData(std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);
    void OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame);

    void SetFlowControl(std::shared_ptr<FlowControl> flow_control) { flow_control_ = flow_control; }
    void SetLocalConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { local_conn_id_manager_ = manager; }
    void SetRemoteConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { remote_conn_id_manager_ = manager; }

private:
    std::shared_ptr<IPacket> MakePacket(IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);
    bool PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer);

private:
    SendControl send_control_;
    // packet number
    PacketNumber pakcet_number_;
    std::shared_ptr<FlowControl> flow_control_;
    std::list<std::shared_ptr<IFrame>> wait_frame_list_;
    std::unordered_set<std::shared_ptr<IStream>> active_send_stream_set_;

    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;
};

}
}

#endif