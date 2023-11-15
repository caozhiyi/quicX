#ifndef QUIC_CONNECTION_CONTROLER_SEND_MANAGER
#define QUIC_CONNECTION_CONTROLER_SEND_MANAGER

#include <list>
#include <memory>
#include <unordered_set>
#include "quic/packet/packet_number.h"
#include "common/timer/timer_interface.h"
#include "quic/packet/packet_interface.h"
#include "common/buffer/buffer_interface.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/connection/controler/send_control.h"
#include "quic/connection/controler/flow_control.h"

namespace quicx {

class SendManager {
public:
    SendManager(std::shared_ptr<ITimer> timer);
    ~SendManager();

    void AddFrame(std::shared_ptr<IFrame> frame);
    void AddActiveStream(std::shared_ptr<IStream> stream);

    bool GetSendData(std::shared_ptr<IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);
    void OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame);

    void SetFlowControl(std::shared_ptr<FlowControl> flow_control) { _flow_control = flow_control; }
    void SetLocalConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { _local_conn_id_manager = manager; }
    void SetRemoteConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { _remote_conn_id_manager = manager; }

private:
    std::shared_ptr<IPacket> MakePacket(uint32_t can_send_size, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);

private:
    SendControl _send_control;
    // packet number
    PacketNumber _pakcet_number;
    std::shared_ptr<FlowControl> _flow_control;
    std::list<std::shared_ptr<IFrame>> _wait_frame_list;
    std::unordered_set<std::shared_ptr<IStream>> _active_send_stream_set;

    // connection id
    std::shared_ptr<ConnectionIDManager> _local_conn_id_manager;
    std::shared_ptr<ConnectionIDManager> _remote_conn_id_manager;
};

}

#endif