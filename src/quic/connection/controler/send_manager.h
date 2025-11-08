#ifndef QUIC_CONNECTION_CONTROLER_SEND_MANAGER
#define QUIC_CONNECTION_CONTROLER_SEND_MANAGER

#include <list>
#include <queue>
#include <memory>
#include <unordered_set>

#include "quic/connection/type.h"
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

    void UpdateConfig(const TransportParam& tp);

    SendOperation GetSendOperation();

    uint32_t GetRtt() { return send_control_.GetRtt(); }
    uint32_t GetPTO(uint32_t max_ack_delay) { return send_control_.GetPTO(max_ack_delay); }
    void ToSendFrame(std::shared_ptr<IFrame> frame);
    void ActiveStream(std::shared_ptr<IStream> stream);

    bool GetSendData(std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);
    void OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame);
    // Reset congestion control and RTT estimator to initial state (on new path)
    void ResetPathSignals();
    // Temporarily disallow stream scheduling (e.g., during path validation / anti-amplification)
    void SetStreamsAllowed(bool allowed) { streams_allowed_ = allowed; }
    // Reset PMTU probing state for a new path (use conservative size until probed)
    void ResetMtuForNewPath();

    // ---- Anti-amplification (unvalidated path) ----
    // Reset anti-amplification budget when entering validation on a new path.
    // Provide a small initial credit to allow sending a PATH_CHALLENGE even if
    // no bytes have been received yet (implementation convenience for probe).
    void ResetAmpBudget();
    // Account bytes received on the candidate path to increase send budget.
    void OnCandidatePathBytesReceived(uint32_t bytes);

    // ---- PMTU probing (skeleton) ----
    // Start a simple PMTU probe sequence after migration (skeleton only).
    void StartMtuProbe();
    // Notify probe result (success selects the higher MTU, failure falls back).
    void OnMtuProbeResult(bool success);

    void SetFlowControl(FlowControl* flow_control) { flow_control_ = flow_control; }
    void SetLocalConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { local_conn_id_manager_ = manager; }
    void SetRemoteConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { remote_conn_id_manager_ = manager; }

private:
    std::shared_ptr<IPacket> MakePacket(IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer);
    bool PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer);
    bool PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer, IFrameVisitor* visitor);
    bool CheckAndChargeAmpBudget(uint32_t bytes);
    bool IsAllowedOnUnvalidated(uint16_t type) const;

private:
    SendControl send_control_;
    // packet number
    PacketNumber pakcet_number_;
    FlowControl* flow_control_;
    std::list<std::shared_ptr<IFrame>> wait_frame_list_;

    // active stream
    std::unordered_set<uint64_t> active_send_stream_ids_;
    std::queue<std::shared_ptr<IStream>> active_send_stream_queue_;

    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;
    friend class BaseConnection;

    bool streams_allowed_ {true};
    uint16_t mtu_limit_bytes_ {1450};

    // Anti-amplification counters for unvalidated path
    uint64_t amp_sent_bytes_ {0};
    uint64_t amp_recv_bytes_ {0};

    // Minimal PMTU probe state (skeleton)
    bool mtu_probe_inflight_ {false};
    uint16_t mtu_probe_target_bytes_ {1450};
    uint64_t mtu_probe_packet_number_ {0};
};

}
}

#endif