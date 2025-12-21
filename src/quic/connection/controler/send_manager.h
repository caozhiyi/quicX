#ifndef QUIC_CONNECTION_CONTROLER_SEND_MANAGER
#define QUIC_CONNECTION_CONTROLER_SEND_MANAGER

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "common/timer/if_timer.h"

#include "quic/connection/connection_id_manager.h"
#include "quic/connection/controler/send_control.h"
#include "quic/connection/controler/send_flow_controller.h"
#include "quic/connection/packet_builder.h"
#include "quic/connection/type.h"
#include "quic/frame/if_frame.h"
#include "quic/packet/packet_number.h"

namespace quicx {
namespace quic {

// Forward declarations
class SendFlowController;
class StreamManager;

class SendManager {
public:
    SendManager(std::shared_ptr<common::ITimer> timer);
    ~SendManager();

    void UpdateConfig(const TransportParam& tp);

    SendOperation GetSendOperation();

    uint32_t GetRtt() { return send_control_.GetRtt(); }
    uint32_t GetPTO(uint32_t max_ack_delay) { return send_control_.GetPTO(max_ack_delay); }
    RttCalculator& GetRttCalculator() { return send_control_.GetRttCalculator(); }
    void ToSendFrame(std::shared_ptr<IFrame> frame);

    // ==================== New High-Level Interfaces ====================

    /**
     * @brief Get available send window size (congestion control)
     * @return Available bytes that can be sent
     */
    uint32_t GetAvailableWindow();

    /**
     * @brief Get pending frames for a specific encryption level
     * @param level Encryption level
     * @param max_bytes Maximum bytes allowed (from congestion window)
     * @return Vector of frames to send
     */
    std::vector<std::shared_ptr<IFrame>> GetPendingFrames(EncryptionLevel level, uint32_t max_bytes);

    /**
     * @brief Check if there is stream data to send
     * @param level Encryption level
     * @return true if stream data is available
     */
    bool HasStreamData(EncryptionLevel level);

    // ==================== Deprecated Interfaces ====================
    void OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame);
    // Reset congestion control and RTT estimator to initial state (on new path)
    void ResetPathSignals();

    // RFC 9002: Check if frames are exempt from congestion control (ACKs, CONNECTION_CLOSE)
    bool IsCongestionControlExempt() const;
    // Temporarily disallow stream scheduling (e.g., during path validation / anti-amplification)
    void SetStreamsAllowed(bool allowed) { streams_allowed_ = allowed; }
    // Reset PMTU probing state for a new path (use conservative size until probed)
    void ResetMtuForNewPath();

    // Set qlog trace for instrumentation
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace);

    // Clear all active streams (used when connection is closing)
    void ClearActiveStreams();
    // Clear retransmission data (used when connection is closing to prevent retransmitting packets)
    void ClearRetransmissionData();

    // ---- Anti-amplification (unvalidated path) ----
    // Reset anti-amplification budget when entering validation on a new path.
    // Provide a small initial credit to allow sending a PATH_CHALLENGE even if
    // no bytes have been received yet (implementation convenience for probe).
    void ResetAmpBudget();
    // Account bytes received on the candidate path to increase send budget.
    void OnCandidatePathBytesReceived(uint32_t bytes);
    // Check if should send Retry (approaching amplification limit)
    bool ShouldSendRetry() const;

    // ---- PMTU probing (skeleton) ----
    // Start a simple PMTU probe sequence after migration (skeleton only).
    void StartMtuProbe();
    // Notify probe result (success selects the higher MTU, failure falls back).
    void OnMtuProbeResult(bool success);

    void SetSendFlowController(SendFlowController* send_flow_controller) {
        send_flow_controller_ = send_flow_controller;
    }
    void SetStreamManager(StreamManager* stream_manager) { stream_manager_ = stream_manager; }
    void SetLocalConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) { local_conn_id_manager_ = manager; }
    void SetRemoteConnectionIDManager(std::shared_ptr<ConnectionIDManager> manager) {
        remote_conn_id_manager_ = manager;
    }
    void SetSendRetryCallBack(std::function<void()> cb) { send_retry_cb_ = cb; }

    void SetToken(const std::string& token) { token_ = token; }
    const std::string& GetToken() const { return token_; }

    // RFC 9000 Section 4.10: Discard packet number space (delegates to SendControl)
    void DiscardPacketNumberSpace(PacketNumberSpace ns) { send_control_.DiscardPacketNumberSpace(ns); }

    // Reset Initial packet number to 0 (used for Retry)
    void ResetInitialPacketNumber() {
        send_control_.ResetInitialPacketNumber();
        pakcet_number_.Reset(kInitialNumberSpace);
    }

    // Accessors for BaseConnection (needed for TrySend)
    PacketNumber& GetPacketNumber() { return pakcet_number_; }
    SendControl& GetSendControl() { return send_control_; }

private:
    bool CheckAndChargeAmpBudget(uint32_t bytes);
    bool IsAllowedOnUnvalidated(uint16_t type) const;

private:
    SendControl send_control_;
    // packet number
    PacketNumber pakcet_number_;
    SendFlowController* send_flow_controller_;  // Send-side flow controller
    StreamManager* stream_manager_{nullptr};    // Stream manager for flow scheduling
    std::list<std::shared_ptr<IFrame>> wait_frame_list_;

    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;

    // Packet builder for unified packet construction
    PacketBuilder packet_builder_;
    friend class BaseConnection;

    bool streams_allowed_{true};
    uint16_t mtu_limit_bytes_{1450};

    // Anti-amplification counters for unvalidated path
    uint64_t amp_sent_bytes_{0};
    uint64_t amp_recv_bytes_{0};

    // Minimal PMTU probe state (skeleton)
    bool mtu_probe_inflight_{false};
    uint16_t mtu_probe_target_bytes_{1450};
    uint64_t mtu_probe_packet_number_{0};

    std::shared_ptr<common::ITimer> timer_;
    common::TimerTask pacing_timer_task_;
    std::function<void()> send_retry_cb_;
    bool is_cwnd_limited_{false};

    // Qlog trace for instrumentation
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    // Retry token
    std::string token_;
};

}  // namespace quic
}  // namespace quicx

#endif