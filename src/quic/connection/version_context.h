#ifndef QUIC_CONNECTION_VERSION_CONTEXT
#define QUIC_CONNECTION_VERSION_CONTEXT

#include <cstdint>
#include <functional>

namespace quicx {
namespace quic {

// Value type encapsulating all QUIC version negotiation state (RFC 9000 §6, RFC 9368, RFC 9369).
// Replaces scattered version-related fields previously spread across BaseConnection.
struct VersionContext {
    // RFC 9369: the on-wire QUIC version currently in use (defaults to QUICv2).
    uint32_t quic_version = 0;

    // RFC 9368: the version the client used in its FIRST Initial packet.
    // Used to re-derive Initial keys if the endpoint later upgrades to a compatible
    // preferred version (same DCID). Also used by the client for the mandatory
    // consistency check that the server's chosen_version matches the version on the wire.
    uint32_t original_version = 0;

    // RFC 9368 §3: Application-preferred QUIC version. When this differs from
    // |quic_version| we advertise willingness to upgrade by including it at the
    // head of |available_versions| in the version_information TP.
    // 0 => "no explicit preference".
    uint32_t preferred_version = 0;

    // RFC 9000 §6: the version chosen by traditional version negotiation.
    uint32_t negotiated_version = 0;

    // True if version negotiation is needed (we received a VN packet).
    bool version_negotiation_needed = false;

    // Prevents infinite version negotiation loops.
    bool version_negotiation_done = false;

    // RFC 9368: set once the local endpoint has executed the compatible version
    // switch (or decided no switch is needed). Makes the switch idempotent.
    bool compat_vn_completed = false;

    // True on server connections (used for RFC 9368 logic that differs by role).
    bool is_server = false;

    // --- Helpers ---

    // Returns the effective preferred version: explicit if set, otherwise quic_version.
    uint32_t GetEffectivePreferredVersion() const {
        return preferred_version ? preferred_version : quic_version;
    }
};

}  // namespace quic
}  // namespace quicx

#endif