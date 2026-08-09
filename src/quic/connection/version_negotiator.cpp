#include "quic/connection/version_negotiator.h"

#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/qlog/qlog.h"
#include "quic/common/constants.h"
#include "quic/connection/error.h"
#include "quic/common/version.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {

VersionNegotiator::VersionNegotiator(bool is_server, ConnectionCrypto& crypto, TransportParam& transport_param)
    : is_server_(is_server), crypto_(crypto), transport_param_(transport_param) {
    ctx_.is_server = is_server;
    ctx_.quic_version = kQuicVersion2;
}

void VersionNegotiator::ApplyVersion(uint32_t version) {
    ctx_.quic_version = version;
    crypto_.SetVersion(version);

    // RFC 9368 §4: our version_information TP must advertise chosen_version ==
    // the version actually on the wire. Re-encode and hand the fresh bytes to
    // TLS before it serializes them, or the peer will (correctly) reject the
    // connection over the mismatch. Skipping this was previously up to each
    // caller to remember.
    RebuildAndPushVersionInformation();
}

VersionNegotiator::UpgradeResult VersionNegotiator::ApplyCompatibleUpgrade(uint32_t new_version) {
    // RFC 9368 §4: derive new Initial keys using the new version's salt and
    // labels, with the SAME DCID the client used for its first Initial. Where
    // that DCID lives depends on role, which is why this is resolved here
    // rather than passed in.
    const std::string& dcid =
        is_server_ ? transport_param_.GetOriginalDestinationConnectionId() : crypto_.GetInitialSecretDcid();

    if (dcid.empty()) {
        LOG_ERROR("RFC 9368: cannot upgrade to 0x%08x, no DCID available (is_server=%d)", new_version,
            static_cast<int>(is_server_));
        return UpgradeResult::kNoDcid;
    }

    LOG_INFO("RFC 9368: upgrading connection from 0x%08x to 0x%08x", ctx_.quic_version, new_version);

    if (!crypto_.RekeyInitialForVersion(new_version, reinterpret_cast<const uint8_t*>(dcid.data()),
            static_cast<uint32_t>(dcid.size()), is_server_)) {
        LOG_ERROR("RFC 9368: RekeyInitialForVersion failed");
        return UpgradeResult::kRekeyFailed;
    }

    // RekeyInitialForVersion already moved the crypto layer's copy; ApplyVersion
    // makes that explicit and covers the other two copies.
    ApplyVersion(new_version);

    ctx_.compat_vn_completed = true;
    common::Metrics::CounterInc(common::MetricsStd::VersionNegotiationTotal);
    return UpgradeResult::kUpgraded;
}

void VersionNegotiator::RecordPeerOriginalVersion(uint32_t pkt_version) {
    if (pkt_version != 0 && ctx_.original_version == 0) {
        ctx_.original_version = pkt_version;
    }
}

void VersionNegotiator::BuildLocalVersionInformation(TransportParam& tp) const {
    // RFC 9368 §3: Build the local version_information TP value.
    //   chosen_version     = current |quic_version| (the version actually on
    //                        the wire in our Initial packets).
    //   available_versions = versions we are willing to speak for this
    //                        connection, in preference order (most preferred
    //                        first).
    //
    // To keep interop with peers that predate RFC 9368 predictable, and to
    // avoid unsolicited upgrades in "default" scenarios, we only widen the
    // list beyond |quic_version| when the application explicitly expressed a
    // different preferred version via |SetPreferredVersion|.
    //   - No preference set              => [quic_version]  (1 entry)
    //   - Preference == quic_version     => [quic_version]  (1 entry)
    //   - Preference != quic_version     => [preferred_version, quic_version]
    std::vector<uint32_t> available;
    const uint32_t pref = (ctx_.preferred_version != 0) ? ctx_.preferred_version : ctx_.quic_version;
    if (pref != ctx_.quic_version) {
        // Sanity-check: we only advertise versions we actually support.
        bool pref_supported = false;
        for (size_t i = 0; i < kQuicVersionsCount; i++) {
            if (kQuicVersions[i] == pref) {
                pref_supported = true;
                break;
            }
        }
        if (pref_supported) {
            available.push_back(pref);
        }
    }
    available.push_back(ctx_.quic_version);

    // RFC 9368 §4: when a compatible-version upgrade occurred (the peer's
    // original version differs from the version we ended up speaking) we MUST
    // advertise that original version in available_versions. Peers such as
    // msquic reject the connection with VERSION_NEGOTIATION_ERROR (0x11) when
    // the negotiated version is absent from the version list they started
    // with. On a server this is reached after ApplyCompatibleUpgrade() set
    // |original_version| to the client's chosen_version; on a client
    // |original_version| stays 0 and this branch is a no-op.
    if (ctx_.original_version != 0 && ctx_.original_version != ctx_.quic_version) {
        bool already_present = false;
        for (uint32_t v : available) {
            if (v == ctx_.original_version) {
                already_present = true;
                break;
            }
        }
        if (!already_present) {
            available.push_back(ctx_.original_version);
        }
    }


    tp.SetVersionInformation(ctx_.quic_version, available);
}

bool VersionNegotiator::RebuildAndPushVersionInformation() {
    if (!transport_param_.HasVersionInformation()) {
        return false;
    }
    BuildLocalVersionInformation(transport_param_);
    if (!push_tp_cb_) {
        return false;
    }
    return push_tp_cb_(transport_param_);
}

bool VersionNegotiator::ValidateAndMaybeUpgradeByRemoteTP(const TransportParam& remote_tp) {
    // RFC 9368 §4: If the peer did not send version_information, there is nothing
    // to validate. This is also the case for endpoints speaking a version that
    // predates RFC 9368 — interop with those must continue to work.
    if (!remote_tp.HasVersionInformation()) {
        return true;
    }

    const uint32_t peer_chosen = remote_tp.GetChosenVersion();
    const std::vector<uint32_t>& peer_available = remote_tp.GetAvailableVersions();

    // RFC 9368 §4: The peer's chosen_version MUST match the version used on the
    // wire for the Initial packets that carried these transport parameters.
    //   - For client-received (server) TP: peer_chosen must equal quic_version
    //     (the version of server-sent Initial packets, which by now equals what
    //     we have been decrypting with).
    //   - For server-received (client) TP: peer_chosen must equal the version
    //     the client used for its FIRST Initial (original_version), which was
    //     recorded when the server first processed that packet. If the server
    //     has not yet recorded original_version, fall back to current.
    const uint32_t expected_peer_on_wire =
        is_server_ ? (ctx_.original_version != 0 ? ctx_.original_version : ctx_.quic_version) : ctx_.quic_version;

    if (peer_chosen != expected_peer_on_wire) {
        LOG_ERROR("RFC 9368: peer chosen_version 0x%08x does not match version used on wire 0x%08x", peer_chosen,
            expected_peer_on_wire);
        CloseConnection(QuicErrorCode::kVersionNegotiationError, 0, "version_information chosen_version mismatch");
        return false;
    }

    if (!is_server_) {
        // Client-side downgrade detection (RFC 9368 §4):
        // If the application explicitly specified a |preferred_version| that
        // differs from the version we actually negotiated (quic_version), and
        // the server's |available_versions| advertises that preferred version,
        // then a MITM may have stripped or rewrote our Initial to force a
        // downgrade.  Close with VERSION_NEGOTIATION_ERROR in that case.
        //
        // Without an explicit preference we have no way to know the "expected"
        // outcome, so we don't invent one.
        if (ctx_.preferred_version != 0 && ctx_.preferred_version != ctx_.quic_version) {
            for (uint32_t sv : peer_available) {
                if (sv == ctx_.preferred_version) {
                    LOG_ERROR(
                        "RFC 9368: downgrade detected: server advertises preferred 0x%08x "
                        "but connection ended up on 0x%08x",
                        ctx_.preferred_version, ctx_.quic_version);
                    CloseConnection(
                        QuicErrorCode::kVersionNegotiationError, 0, "Compatible Version downgrade detected");
                    return false;
                }
            }
        }
        // Client side: no further action. Initial key rekey (if any) has
        // already happened via OnInitialPacket when the server's v2 Initial
        // arrived.
        ctx_.compat_vn_completed = true;
        return true;
    }

    // ------- Server side -------
    // Decide whether to upgrade from the client's chosen_version to a version we
    // prefer more. We only consider upgrading to |preferred_version| (if the
    // application explicitly set one and it differs from |quic_version|), and
    // only when the client's available_versions list also contains it. This
    // keeps the default path (no preference) conservative: a server without an
    // explicit preference just stays on the client's chosen_version.
    if (ctx_.compat_vn_completed) {
        return true;  // Already upgraded (or decided not to).
    }

    uint32_t negotiated = peer_chosen;  // Default: stay on client's chosen version.
    const uint32_t our_pref = (ctx_.preferred_version != 0) ? ctx_.preferred_version : ctx_.quic_version;
    if (our_pref != peer_chosen) {
        for (uint32_t cv : peer_available) {
            if (cv == our_pref) {
                negotiated = our_pref;
                break;
            }
        }
    }

    if (negotiated == ctx_.quic_version) {
        // No version change; but still record original_version for later
        // consistency bookkeeping.
        RecordPeerOriginalVersion(peer_chosen);
        ctx_.compat_vn_completed = true;
        return true;
    }

    // Upgrade. Record the version the client used before we move.
    RecordPeerOriginalVersion(peer_chosen);

    switch (ApplyCompatibleUpgrade(negotiated)) {
        case UpgradeResult::kUpgraded:
            return true;

        case UpgradeResult::kNoDcid:
            // Server tolerates this: fall back to the client's chosen_version
            // rather than failing the connection.
            LOG_ERROR("RFC 9368: cannot upgrade, original_destination_connection_id missing");
            ctx_.compat_vn_completed = true;
            return true;

        case UpgradeResult::kRekeyFailed:
        default:
            CloseConnection(QuicErrorCode::kInternalError, 0, "Compatible VN rekey failed");
            return false;
    }
}

bool VersionNegotiator::OnVersionNegotiationPacket(const std::shared_ptr<IPacket>& packet) {
    auto vn_packet = std::dynamic_pointer_cast<VersionNegotiationPacket>(packet);
    if (!vn_packet) {
        LOG_ERROR("Failed to cast to VersionNegotiationPacket");
        return false;
    }

    auto supported_versions = vn_packet->GetSupportVersion();
    LOG_WARN("Received Version Negotiation packet with %zu supported versions", supported_versions.size());

    // RFC 9000 §6.2: Discard if our version is listed (downgrade attack)
    if (IsVnDowngradeAttack(supported_versions)) {
        return true;
    }

    // RFC 9000 §6: Version negotiation should only happen once
    if (ctx_.version_negotiation_done) {
        LOG_ERROR("Received Version Negotiation packet after already negotiating version - closing connection");
        CloseConnection(QuicErrorCode::kProtocolViolation, 0, "Version negotiation attempted multiple times");
        return true;
    }

    // Select a compatible version and trigger callback
    uint32_t compatible_version = SelectVersion(supported_versions);
    HandleCompatibleVersionFound(compatible_version);

    common::Metrics::CounterInc(common::MetricsStd::VersionNegotiationTotal);
    return true;
}

bool VersionNegotiator::IsVnDowngradeAttack(const std::vector<uint32_t>& supported_versions) const {
    uint32_t our_version = ctx_.quic_version;
    for (auto version : supported_versions) {
        if (version == our_version) {
            LOG_WARN("Version Negotiation lists our current version 0x%08x - possible attack!", our_version);
            if (qlog_trace_) {
                common::PacketDroppedData drop_data;
                drop_data.packet_type = PacketType::kNegotiationPacketType;
                drop_data.trigger = "version_negotiation_downgrade";
                QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
            }
            return true;
        }
    }
    return false;
}

void VersionNegotiator::HandleCompatibleVersionFound(uint32_t compatible_version) {
    uint32_t our_version = ctx_.quic_version;
    if (compatible_version != 0 && compatible_version != our_version) {
        LOG_INFO("Found compatible version: 0x%08x (%s), will reconnect", compatible_version,
            VersionToString(compatible_version));
        ctx_.negotiated_version = compatible_version;
        ctx_.version_negotiation_needed = true;

        if (version_negotiation_cb_) {
            version_negotiation_cb_(compatible_version);
        } else {
            LOG_WARN("Version negotiation callback not set, closing connection");
            CloseConnection(QuicErrorCode::kVersionNegotiationError, 0, "Version negotiation required but no handler");
        }
    } else {
        LOG_ERROR("No compatible QUIC version found in server's list");
        CloseConnection(QuicErrorCode::kVersionNegotiationError, 0, "No compatible QUIC version");
    }
}

void VersionNegotiator::CloseConnection(uint64_t error, uint16_t trigger_frame, std::string reason) {
    if (close_connection_cb_) {
        close_connection_cb_(error, trigger_frame, std::move(reason));
    }
}

}  // namespace quic
}  // namespace quicx
