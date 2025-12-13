#ifndef QUIC_CONNECTION_PATH_MANAGER_H
#define QUIC_CONNECTION_PATH_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "common/network/address.h"
#include "common/timer/timer_task.h"

namespace quicx {

// Forward declarations from common namespace
namespace common {
class IEventLoop;
}

namespace quic {

// Forward declarations
class SendManager;
class ConnectionIDCoordinator;
class TransportParam;
class IFrame;

/**
 * @brief Path manager for connection migration and path validation
 *
 * Responsibilities:
 * - Path validation (PATH_CHALLENGE/PATH_RESPONSE)
 * - Path migration support
 * - Anti-amplification protection during path validation
 * - Candidate address queue management
 */
class PathManager {
public:
    using ToSendFrameCallback = std::function<void(std::shared_ptr<IFrame>)>;
    using ActiveSendCallback = std::function<void()>;
    using SetPeerAddressCallback = std::function<void(const ::quicx::common::Address&)>;

    PathManager(std::shared_ptr<::quicx::common::IEventLoop> event_loop,
                SendManager& send_manager,
                ConnectionIDCoordinator& cid_coordinator,
                TransportParam& transport_param,
                ::quicx::common::Address& peer_addr,
                ToSendFrameCallback to_send_frame_cb,
                ActiveSendCallback active_send_cb,
                SetPeerAddressCallback set_peer_addr_cb);

    ~PathManager() = default;

    // ==================== Path Validation ====================

    /**
     * @brief Start path validation probe to candidate address
     * Sends PATH_CHALLENGE and schedules retries
     */
    void StartPathValidationProbe();

    /**
     * @brief Start probing next address in queue
     * Pops next candidate from queue and starts validation
     */
    void StartNextPathProbe();

    /**
     * @brief Handle PATH_RESPONSE frame
     * Validates token and promotes candidate to active path if matched
     * @param data Challenge data from PATH_RESPONSE
     */
    void OnPathResponse(const uint8_t* data);

    /**
     * @brief Handle PATH_CHALLENGE frame and generate response
     * @param data Challenge data to echo back
     * @param response_frame Output parameter for PATH_RESPONSE frame
     */
    void OnPathChallenge(const uint8_t* data, std::shared_ptr<IFrame>& response_frame);

    // ==================== Observed Address Handling ====================

    /**
     * @brief Handle observed peer address change (potential migration/NAT rebinding)
     * @param addr New observed peer address
     */
    void OnObservedPeerAddress(const ::quicx::common::Address& addr);

    /**
     * @brief Record bytes received from candidate path (for anti-amp budget)
     * @param bytes Number of bytes received
     */
    void OnCandidatePathBytesReceived(uint32_t bytes);

    // ==================== Anti-Amplification ====================

    /**
     * @brief Enter anti-amplification state (restrict sending while path unvalidated)
     */
    void EnterAntiAmplification();

    /**
     * @brief Exit anti-amplification state (path validated, allow normal sending)
     */
    void ExitAntiAmplification();

    // ==================== Send Address Selection ====================

    /**
     * @brief Get address to send packets to
     * Returns candidate address if probing, otherwise active peer address
     * @return Address to use for sending
     */
    ::quicx::common::Address GetSendAddress() const;

    // ==================== State Queries ====================

    /**
     * @brief Check if path probe is in flight
     * @return true if currently probing a path
     */
    bool IsPathProbeInflight() const { return path_probe_inflight_; }

    /**
     * @brief Get candidate peer address
     * @return Current candidate address being validated
     */
    const ::quicx::common::Address& GetCandidatePeerAddress() const { return candidate_peer_addr_; }

private:
    /**
     * @brief Schedule retry of path probe
     * Uses exponential backoff with max retries
     */
    void ScheduleProbeRetry();

private:
    // Dependencies (injected)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    SendManager& send_manager_;
    ConnectionIDCoordinator& cid_coordinator_;
    TransportParam& transport_param_;
    ::quicx::common::Address& peer_addr_;  // Reference to main connection address
    ToSendFrameCallback to_send_frame_cb_;
    ActiveSendCallback active_send_cb_;
    SetPeerAddressCallback set_peer_addr_cb_;

    // Path validation state
    ::quicx::common::Address candidate_peer_addr_;
    bool path_probe_inflight_{false};
    uint8_t pending_path_challenge_data_[8]{0};
    ::quicx::common::TimerTask path_probe_task_;
    uint32_t probe_retry_count_{0};
    uint32_t probe_retry_delay_ms_{0};

    // Queue of pending candidate addresses for path validation
    std::vector<::quicx::common::Address> pending_candidate_addrs_;

    // Probe retry limits
    static constexpr uint32_t kMaxProbeRetries = 5;
    static constexpr uint32_t kInitialProbeDelayMs = 100;
    static constexpr uint32_t kMaxProbeDelayMs = 2000;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_PATH_MANAGER_H
