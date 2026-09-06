#ifndef QUIC_CONNECTION_VERSION_NEGOTIATOR
#define QUIC_CONNECTION_VERSION_NEGOTIATOR

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/log/log.h"

#include "quic/connection/connection_crypto.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/version_context.h"
#include "quic/packet/if_packet.h"

namespace quicx {
namespace common {
class QlogTrace;
}
namespace quic {

/**
 * @brief Sole owner of QUIC version state (RFC 9000 §6, RFC 9368, RFC 9369).
 *
 * The on-wire version has to agree in three places at once:
 *
 *   1. VersionContext::quic_version  — what this layer believes we speak
 *   2. ConnectionCrypto::quic_version — drives Initial-secret salt + labels,
 *      and is what the send path actually reads (via GetVersion())
 *   3. the version_information transport parameter handed to TLS — RFC 9368 §4
 *      requires chosen_version to equal the version on the wire, and the peer
 *      MUST reject the connection if it does not
 *
 * Keeping those three in step was previously nobody's job. `SetVersion()` on
 * BaseConnection updated (1) and (2) but not (3), so every caller had to
 * remember to also call RebuildAndPushVersionInformation() — and one caller
 * skipped the setter entirely and assigned `version_ctx_.quic_version`
 * directly, relying on the fact that RekeyInitialForVersion() happens to set
 * the crypto copy a few lines earlier. That worked, but only by coincidence:
 * nothing at the assignment site said so.
 *
 * Here VersionContext is private, so no such write is expressible. Every
 * version change goes through ApplyVersion() or ApplyCompatibleUpgrade(), both
 * of which update all three.
 *
 * The compatible-upgrade path also used to exist twice — once for the client
 * (detecting the server's switch on an incoming Initial) and once for the
 * server (acting on the client's version_information TP). The two differ only
 * in role and in where the DCID comes from, both of which this class already
 * knows, so they are now one function.
 */
class VersionNegotiator {
public:
    typedef std::function<void(uint32_t new_version)> version_negotiation_callback;

    /**
     * @brief Re-encode the local transport parameters and hand them to TLS.
     *
     * A callback rather than a direct dependency because the same encode+push
     * step also serves the general (non-version) transport-parameter path at
     * connection setup, which is not this class's business.
     */
    using PushTransportParamCallback = std::function<bool(TransportParam&)>;

    using CloseConnectionCallback = std::function<void(uint64_t error, uint16_t trigger_frame, std::string reason)>;

    /**
     * @brief Outcome of a compatible-version upgrade attempt.
     *
     * The two call sites disagree on how bad a missing DCID is — the server
     * tolerates it and proceeds without upgrading, the client cannot decrypt
     * and must drop the packet — so the distinction is reported rather than
     * decided here.
     */
    enum class UpgradeResult {
        kUpgraded,     // version changed and Initial keys re-derived
        kNoDcid,       // cannot upgrade: no DCID to re-derive from
        kRekeyFailed,  // re-derivation failed; fatal
    };

    VersionNegotiator(bool is_server, ConnectionCrypto& crypto, TransportParam& transport_param);

    ~VersionNegotiator() = default;

    VersionNegotiator(const VersionNegotiator&) = delete;
    VersionNegotiator& operator=(const VersionNegotiator&) = delete;

    void SetPushTransportParamCallback(PushTransportParamCallback cb) { push_tp_cb_ = std::move(cb); }
    void SetCloseConnectionCallback(CloseConnectionCallback cb) { close_connection_cb_ = std::move(cb); }
    void SetVersionNegotiationCallback(version_negotiation_callback cb) { version_negotiation_cb_ = std::move(cb); }
    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) { qlog_trace_ = std::move(trace); }

    // ==================== The only twoways to change version ====================

    /**
     * @brief Move to |version|, keeping all three copies consistent.
     *
     * Use when no key re-derivation is needed (e.g. adopting the version seen
     * on the first inbound Initial, before any Initial secret is installed).
     */
    void ApplyVersion(uint32_t version);

    /**
     * @brief RFC 9368 §4 compatible-version upgrade.
     *
     * Re-derives the Initial secret under the new version's salt using the
     * SAME DCID (§4: the DCID does not change across a compatible VN), then
     * applies the version. The DCID source depends on role and is resolved
     * internally so a caller cannot pass the wrong one:
     *   - server: original_destination_connection_id from our own TP, i.e. the
     *     DCID the client put in its first Initial
     *   - client: the DCID cached by ConnectionCrypto when it installed the
     *     original Initial secret, which is the same value the server rekeyed
     *     against
     */
    UpgradeResult ApplyCompatibleUpgrade(uint32_t new_version);

    // ==================== Queries ====================

    uint32_t GetVersion() const { return ctx_.quic_version; }
    bool IsServer() const { return is_server_; }

    void SetPreferredVersion(uint32_t version) { ctx_.preferred_version = version; }
    uint32_t GetPreferredVersion() const { return ctx_.GetEffectivePreferredVersion(); }

    void SetVersionNegotiationDone() { ctx_.version_negotiation_done = true; }

    bool IsCompatVnCompleted() const { return ctx_.compat_vn_completed; }
    void MarkCompatVnCompleted() { ctx_.compat_vn_completed = true; }

    /**
     * @brief Record the version the peer used in its FIRST Initial.
     *
     * The server needs this for the mandatory RFC 9368 §4 check that the
     * client's chosen_version matches what was actually on the wire. Only the
     * first value sticks.
     */
    void RecordPeerOriginalVersion(uint32_t pkt_version);

    /**
     * @brief True if |pkt_version| means the server switched version on us.
     *
     * Client-side only: a version change on an inbound Initial after we already
     * have Initial keys installed means we must re-key before we can decrypt.
     */
    bool IsPeerVersionSwitch(uint32_t pkt_version) const {
        return !is_server_ && pkt_version != 0 && pkt_version != ctx_.quic_version;
    }

    /**
     * @brief True if |pkt_version| differs from what we currently speak.
     */
    bool DiffersFromCurrent(uint32_t pkt_version) const { return pkt_version != 0 && pkt_version != ctx_.quic_version; }

    // ==================== Protocol handlers ====================

    /**
     * @brief RFC 9368 §4 validation of the peer's version_information, plus the
     *        server-side compatible upgrade it may trigger.
     *
     * @return false if the connection was closed as inconsistent.
     */
    bool ValidateAndMaybeUpgradeByRemoteTP(const TransportParam& remote_tp);

    /**
     * @brief Fill in the version_information TP (RFC 9368 §3) for |tp|.
     */
    void BuildLocalVersionInformation(TransportParam& tp) const;

    /**
     * @brief Re-encode version_information and push the fresh TP bytes to TLS.
     */
    bool RebuildAndPushVersionInformation();

    /**
     * @brief RFC 9000 §6.2 Version Negotiation packet handling.
     */
    bool OnVersionNegotiationPacket(const std::shared_ptr<IPacket>& packet);

private:
    // RFC 9000 §6.2: a VN packet listing our own version is an attack signal.
    bool IsVnDowngradeAttack(const std::vector<uint32_t>& supported_versions) const;

    void HandleCompatibleVersionFound(uint32_t compatible_version);

    void CloseConnection(uint64_t error, uint16_t trigger_frame, std::string reason);

    // Private: the whole point. External code cannot write these fields, so the
    // three-way consistency cannot be broken from outside.
    VersionContext ctx_;

    const bool is_server_;
    ConnectionCrypto& crypto_;
    TransportParam& transport_param_;

    PushTransportParamCallback push_tp_cb_;
    CloseConnectionCallback close_connection_cb_;
    version_negotiation_callback version_negotiation_cb_;
    std::shared_ptr<common::QlogTrace> qlog_trace_;
};

}  // namespace quicx
}  // namespace quicx

#endif  // QUIC_CONNECTION_VERSION_NEGOTIATOR
