#ifndef QUIC_CONNECTION_CONTROLER_SEND_MANAGER
#define QUIC_CONNECTION_CONTROLER_SEND_MANAGER

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "common/timer/if_timer.h"

#include "quic/connection/connection_id_manager.h"
#include "quic/connection/controler/anti_amplification_controller.h"
#include "quic/connection/controler/pmtu_prober.h"
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
    SendManager(std::shared_ptr<common::ITimerScheduler> scheduler);
    ~SendManager();

    void UpdateConfig(const TransportParam& tp);

    // Forward the connection's lifetime guard to SendControl so its PTO /
    // retransmit timers are skipped once the connection is destroyed (see
    // SendControl::Bind). Called from BaseConnection once it is owned.
    void Bind(std::weak_ptr<void> self) { send_control_.Bind(self); }

    SendOperation GetSendOperation();

    uint32_t GetRtt() { return send_control_.GetRtt(); }
    uint32_t GetPTO(uint32_t max_ack_delay) { return send_control_.GetPTO(max_ack_delay); }
    RttCalculator& GetRttCalculator() { return send_control_.GetRttCalculator(); }
    void EnqueueFrame(std::shared_ptr<IFrame> frame);

    /**
     * @brief Whether any queued frame is a probing frame.
     *
     * A probing frame here means PATH_CHALLENGE or PATH_RESPONSE, matching the
     * RFC 9000 §9.3.3 congestion-window exemption (packets containing only
     * probing frames may be sent even when cwnd is exhausted, so that path
     * validation cannot deadlock behind congestion control).
     *
     * Note this is deliberately narrower than the §9.1 definition of a probing
     * packet (which also admits NEW_CONNECTION_ID and PADDING): only the two
     * frames that actually drive path validation earn the cwnd bypass.
     *
     * Replaces direct access to wait_frame_list_ from BaseConnection.
     */
    bool HasPendingProbingFrame() const;

    // ==================== New High-Level Interfaces ====================

    /**
     * @brief Get available send window size (congestion control)
     * @return Available bytes that can be sent
     */
    uint32_t GetAvailableWindow();

    /**
     * @brief Notify that congestion window is limiting send
     * This should be called when TrySend() finds cwnd=0
     * When ACK is received, send_retry_cb_ will be called to resume sending
     */
    void SetCwndLimited();

    /**
     * @brief Notify that connection-level flow control is blocking send
     *
     * Called by BaseConnection::TrySend when SendFlowController::CanSendData
     * returns false (sent_bytes_ >= peer's max_data_) and the streams have
     * data to send but no congestion-window pressure.
     *
     * Without this signal the worker would observe TrySend() returning false,
     * remove the connection from its active set, and the connection would
     * fall completely idle: no ACKs in flight (peer already acked everything),
     * no MAX_DATA from the peer (peer hasn't consumed enough to extend the
     * window), so nothing wakes the connection up until idle timeout fires.
     *
     * RFC 9000 Bug #17 fix: schedule a low-frequency recheck timer (default
     * 100ms) so the connection is re-examined periodically. Concurrently,
     * any incoming ACK (see OnPacketAck) will also resume sending — covering
     * the case where the peer happens to send MAX_DATA along with an ACK.
     */
    void SetFlowControlBlocked();

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
    // The default initial credit lets a PATH_CHALLENGE go out even though no bytes
    // have yet been received on the candidate path (an implementation convenience
    // for probing). Pass 0 for a strict RFC 9000 §8.1 budget, which is what a
    // server must use for a brand-new connection from an unvalidated address:
    // there the client's Initial supplies the credit, and a non-zero starting
    // credit would hand the attacker free amplification.
    void ResetAmpBudget(uint64_t initial_credit = kProbeInitialCredit);
    static constexpr uint64_t kProbeInitialCredit = 400;
    // Account bytes received on the candidate path to increase send budget.
    void OnCandidatePathBytesReceived(uint32_t bytes);
    // Check if should send Retry (approaching amplification limit)
    bool ShouldSendRetry() const;

    /**
     * @brief RFC 9000 §8.1 gate: may we put `bytes` more on the wire right now?
     *
     * Charges the budget on success. Must be called for the full UDP datagram
     * length, not the payload length, since the limit is defined over datagrams.
     * Returns true unconditionally once the peer address has been validated.
     *
     * Public because the enforcement point is DatagramEmitter (the single egress
     * choke point), not SendManager itself.
     */
    bool CheckAndChargeAmpBudget(uint32_t bytes);

    /**
     * @brief Mark the peer address as validated, lifting the §8.1 budget.
     *
     * Called once the address is proven: for a server, on successfully processing
     * a Handshake packet from the peer, or on a validated Retry token.
     */
    void MarkAddressValidated();

    /**
     * @brief Whether the last send attempt was refused by the §8.1 budget.
     *
     * The worker uses this to avoid spinning on a connection that cannot send
     * until more bytes arrive from the peer.
     */
    bool IsAmpBlocked() const { return amp_blocked_; }

    /**
     * @brief Read-only view of the RFC 9000 §8.1 state, for tests only.
     *
     * The controller was previously instantiated but never entered, so every
     * assertion about the limit had to be written against the controller in
     * isolation and passed while the connection ignored it entirely. Tests need
     * to reach the *connection's own* controller to catch that class of bug.
     */
    const AntiAmplificationController& GetAmpControllerForTest() const { return amp_controller_; }

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

    // Reset Initial packet state for Retry (clear unacked/lost packets but keep PN counter)
    // RFC 9000 Section 17.2.5.3: PN must NOT be reset after Retry
    void ResetInitialPacketNumber() {
        send_control_.ResetInitialPacketNumber();
        // Do NOT reset packet_number_ — interop tests require PN to be
        // strictly greater than the highest PN sent before Retry
    }

    // Accessors for BaseConnection (needed for TrySend)
    PacketNumber& GetPacketNumber() { return packet_number_; }
    SendControl& GetSendControl() { return send_control_; }

private:
    bool IsAllowedOnUnvalidated(uint16_t type) const;

private:
    SendControl send_control_;
    // packet number
    PacketNumber packet_number_;
    // Both are owned by the enclosing BaseConnection and outlive this manager. They are
    // wired in via SetSendFlowController/SetStreamManager after construction, so they
    // must default to null: a non-null garbage value here would be indistinguishable
    // from a wired-up pointer.
    SendFlowController* send_flow_controller_{nullptr};  // Send-side flow controller
    StreamManager* stream_manager_{nullptr};             // Stream manager for flow scheduling
    std::list<std::shared_ptr<IFrame>> wait_frame_list_;

    // connection id
    std::shared_ptr<ConnectionIDManager> local_conn_id_manager_;
    std::shared_ptr<ConnectionIDManager> remote_conn_id_manager_;

    // Packet builder for unified packet construction
    PacketBuilder packet_builder_;

    bool streams_allowed_{true};

    // PMTU prober (encapsulates MTU discovery logic)
    PmtuProber pmtu_prober_;

    // Anti-amplification controller for unvalidated path
    AntiAmplificationController amp_controller_;
    // Set when CheckAndChargeAmpBudget() last refused a datagram; cleared as soon
    // as fresh bytes from the peer restore some budget. Atomic because it is
    // written on the send thread and read on the worker thread (IsAmpBlocked()).
    std::atomic<bool> amp_blocked_{false};

    std::shared_ptr<common::ITimerScheduler> scheduler_;
    // Guards the pacing and flow-control callbacks, which capture a raw `this`.
    std::shared_ptr<int> life_token_ = std::make_shared<int>(0);
    common::Timer pacing_timer_;
    std::function<void()> send_retry_cb_;
    bool is_cwnd_limited_{false};

    // Bug #17: connection-level flow control coordination.
    //   - is_flow_control_blocked_:  set by SetFlowControlBlocked() when the
    //     server has stream data buffered but the peer's connection-level
    //     max_data has been exhausted.
    //   - flow_control_recheck_timer_: a low-frequency wake-up that fires every
    //     kFlowControlRecheckIntervalMs (100ms) while the flag is set, so the
    //     connection re-enters TrySend even if the peer never sends MAX_DATA
    //     and there are no in-flight packets to trigger an ACK callback.
    //   - flow_control_recheck_scheduled_: prevents redundant timer additions
    //     while one is already pending.
    bool is_flow_control_blocked_{false};
    bool flow_control_recheck_scheduled_{false};
    common::Timer flow_control_recheck_timer_;

    void ArmPacingTimer(uint32_t delay_ms);
    void ArmFlowControlRecheckTimer();

    // Qlog trace for instrumentation
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    // Retry token
    std::string token_;
};

}  // namespace quic
}  // namespace quicx

#endif
