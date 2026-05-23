#ifndef QUIC_CONNECTION_CONNECTION_ID_MANAGER
#define QUIC_CONNECTION_CONNECTION_ID_MANAGER

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>

#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

class ConnectionIDManager {
public:
    // create a new connection id manager
    // add_connection_id_cb: callback when a new connection id is generated
    // retire_connection_id_cb: callback when a connection id is retired
    ConnectionIDManager(std::function<void(ConnectionID&)> add_connection_id_cb = nullptr,
        std::function<void(ConnectionID&)> retire_connection_id_cb = nullptr):
        // RFC 9000 §5.1.1: "The sequence number of the initial connection ID is 0."
        // Generator() pre-increments cur_sequence_number_ before assigning, so we start
        // at -1 to make the first generated CID receive sequence_number = 0. The very
        // first call to Generator() therefore produces the endpoint's *initial SCID*
        // (used in the long-header Source Connection ID field during the handshake);
        // every subsequent Generator() call yields 1, 2, 3, ... which are the values
        // emitted in NEW_CONNECTION_ID frames. Pre-fix the seed was 0, which made the
        // initial SCID claim sequence=1 and caused NEW_CONNECTION_ID frames to start
        // at sequence=2, leaving sequence=1 permanently absent from the peer's pool.
        // Some QUIC stacks (e.g. quic-go) treat that gap as "still waiting for the CID
        // that is supposed to live at sequence 1" and never activate the spare CIDs,
        // which in turn blocks features that depend on a fully populated remote-CID
        // pool such as connection migration and timely connection-flow window growth.
        cur_sequence_number_(-1),
        add_connection_id_cb_(add_connection_id_cb),
        retire_connection_id_cb_(retire_connection_id_cb) {}

    ~ConnectionIDManager() {}

    ConnectionID Generator();
    ConnectionID& GetCurrentID();
    // Replace the current CID without touching sequence_cid_map_ or cur_sequence_number_.
    // Used by the client during handshake to install the server-issued SCID as the active
    // DCID (RFC 9000 §7.2). The CID lives outside the map so frame-driven AddID/RetireID
    // operations on map entries cannot collide with the handshake placeholder.
    void SetCurrentID(const uint8_t* id, uint16_t len, uint64_t sequence);
    bool RetireIDBySequence(uint64_t sequence);
    // Batch retirement helper: retire every CID with sequence_number < prior_to.
    // Used when applying NEW_CONNECTION_ID.retire_prior_to (RFC 9000 §19.15).
    bool RetireIDsUpTo(uint64_t prior_to);
    bool AddID(ConnectionID& id);
    bool AddID(const uint8_t* id, uint16_t len);
    bool UseNextID();

    // Get the number of available CIDs in the pool
    size_t GetAvailableIDCount() const { return sequence_cid_map_.size(); }

    // Get all CIDs managed by this manager (for cleanup on connection close)
    std::vector<uint64_t> GetAllIDHashes();

private:
    ConnectionID cur_id_;
    int64_t cur_sequence_number_;
    std::map<uint64_t, ConnectionID> sequence_cid_map_;

    std::function<void(ConnectionID&)> add_connection_id_cb_;
    std::function<void(ConnectionID&)> retire_connection_id_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif