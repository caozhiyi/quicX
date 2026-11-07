#ifndef HTTP3_QPACK_QPACK_ENCODER
#define HTTP3_QPACK_QPACK_ENCODER

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/buffer/if_buffer.h"

#include "http3/config.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

class QpackEncoder {
public:
    QpackEncoder():
        dynamic_table_(1024),
        max_table_capacity_(1024),
        local_max_table_capacity_(1024),
        peer_max_table_capacity_(0),
        peer_cap_known_(false),
        enable_dynamic_table_(false) {}
    ~QpackEncoder() {}

    // RFC 9204 §3.2.3: The encoder's actual table capacity MUST NOT exceed
    // either side's advertised limit. We therefore split the capacity into:
    //   - local_max_table_capacity_: the value WE configured locally
    //   - peer_max_table_capacity_:  the value PEER advertised via SETTINGS
    //   - max_table_capacity_:       min(local, peer) — only valid once peer
    //                                SETTINGS have been received.
    // Init() and SETTINGS handlers can arrive in any order on a real
    // connection, so both setters recompute the effective cap independently.
    void SetLocalMaxTableCapacity(uint32_t cap) {
        local_max_table_capacity_ = cap;
        RecomputeMaxTableCapacity();
    }
    void SetPeerMaxTableCapacity(uint32_t cap) {
        peer_max_table_capacity_ = cap;
        peer_cap_known_ = true;
        RecomputeMaxTableCapacity();
    }
    // Backward-compat alias used by older call sites: treat as "configure
    // local cap" (matches prior single-field semantics). Prefer the
    // explicit Local/Peer setters above.
    void SetMaxTableCapacity(uint32_t max_capacity) { SetLocalMaxTableCapacity(max_capacity); }
    uint32_t GetMaxTableCapacity() const { return max_table_capacity_; }
    uint32_t GetLocalMaxTableCapacity() const { return local_max_table_capacity_; }
    uint32_t GetPeerMaxTableCapacity() const { return peer_max_table_capacity_; }

    // Get the current insert count of the dynamic table (monotonically increasing)
    uint64_t GetInsertCount() const { return dynamic_table_.GetInsertCount(); }

    // === Known Received Count (RFC 9204 §2.1.4) ===
    // The number of dynamic-table insertions the PEER'S DECODER has confirmed
    // it applied, learned from Insert Count Increment instructions on the
    // peer's decoder stream.
    //
    // RFC 9204 §2.1.2 forbids the encoder from making a stream blocked beyond
    // the peer's SETTINGS_QPACK_BLOCKED_STREAMS limit; a decoder that sees the
    // limit exceeded MUST kill the connection with QPACK_DECOMPRESSION_FAILED.
    // The encoder used to reference every freshly inserted entry via a
    // post-base index in the very same header block, which makes the block's
    // Required Insert Count exceed the peer's applied insert count by
    // construction — i.e. EVERY request/response blocked a stream. Under
    // concurrency that trivially overran the limit and tore down the whole
    // connection.
    //
    // We therefore only reference entries the peer has already acknowledged
    // (abs_index < known_received_count_), which keeps the Required Insert
    // Count at or below the peer's applied count and makes blocking
    // impossible. New entries are still inserted and announced on the encoder
    // stream, so subsequent header blocks get the compression benefit.
    //
    // Known Received Count lives in ONE place -- the DynamicTable -- which also
    // consults it for the eviction guard. (It used to be mirrored here and
    // hand-synchronised; a second copy can only drift.)
    //
    // Section Acknowledgment (RFC 9204 4.4.1) also advances the count, to the
    // acknowledged section's Required Insert Count. This encoder never has that
    // to add: it only references entries with abs_index < known_received_count_
    // (see DecideHeaderEncoding), so every section it emits has
    // RIC <= known_received_count_ and acknowledging it can never raise the
    // count. If the encoder is ever changed to reference not-yet-acknowledged
    // entries (post-base references), Section Ack MUST be wired to
    // AdvanceKnownReceived as well.
    void OnPeerInsertCountIncrement(uint64_t delta) { dynamic_table_.AdvanceKnownReceived(delta); }
    uint64_t GetKnownReceivedCount() const { return dynamic_table_.GetKnownReceivedCount(); }

    // RFC 9204 4.4.1 Section Acknowledgment: the peer's decoder finished a header
    // block on |stream_id|. Acknowledgments are in order per stream, so it
    // retires the EARLIEST outstanding section of that stream: its entries stop
    // being pinned, and Known Received Count advances to at least its Required
    // Insert Count (RFC 9204 2.1.4 -- an acknowledged section proves the
    // decoder applied that many inserts). Returns false if the stream has no
    // outstanding section (a peer error per the RFC; the caller decides how to
    // react -- this class only logs and ignores).
    bool OnPeerSectionAck(uint64_t stream_id);
    // RFC 9204 4.4.2 Stream Cancellation: every outstanding section of the
    // stream is abandoned; nothing will ever acknowledge it.
    void OnPeerStreamCancellation(uint64_t stream_id);

    // Observability / tests.
    size_t GetOutstandingSectionCount() const { return outstanding_count_; }
    static constexpr size_t kMaxOutstandingSections = 4096;

    // Enable or disable dynamic table usage (starts disabled; the connection
    // layer enables it once SETTINGS advertise a non-zero QPACK table).
    void SetDynamicTableEnabled(bool enabled) { enable_dynamic_table_ = enabled; }
    bool IsDynamicTableEnabled() const { return enable_dynamic_table_; }

    // |stream_id| is the stream the encoded block will be SENT on. It keys the
    // outstanding-section bookkeeping that protects referenced dynamic-table
    // entries from eviction until the peer's decoder acknowledges the block
    // (Section Acknowledgment) or abandons the stream (Stream Cancellation),
    // RFC 9204 2.1.1. Every production caller MUST pass it. kNoStreamId means
    // "no decoder-feedback channel" (unit tests, offline tools): the block is
    // still encoded and may reference the dynamic table, but nothing protects
    // those entries afterwards.
    static constexpr uint64_t kNoStreamId = UINT64_MAX;
    bool Encode(const std::vector<std::pair<std::string, std::string>>& headers,
        std::shared_ptr<common::IBuffer> buffer, uint64_t stream_id = kNoStreamId);
    // Set a callback used to send encoder instructions (Insert entries) on QPACK encoder stream
    void SetInstructionSender(std::function<void(const std::vector<std::pair<std::string, std::string>>&)> cb) {
        instruction_sender_ = std::move(cb);
    }
    // Encoder-stream side: generate QPACK encoder instructions (RFC 9204)
    bool EncodeEncoderInstructions(const std::vector<std::pair<std::string, std::string>>& inserts,
        std::shared_ptr<common::IBuffer> instr_buf, bool with_name_ref = false, bool set_capacity = false,
        uint32_t new_capacity = 0, int32_t duplicate_index = -1);
    // HEADERS prefix write/read per RFC 9204
    void WriteHeaderPrefix(std::shared_ptr<common::IBuffer> buffer, uint64_t required_insert_count, int64_t base);
    bool ReadHeaderPrefix(
        const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, int64_t& base);

private:
    void SetEnableDynamicTable(bool enable) { enable_dynamic_table_ = enable; }
    // Recompute effective max_table_capacity_ from local + peer caps.
    // Before peer SETTINGS arrive we honour only the local cap so that
    // outbound encoding can proceed once SETTINGS have been observed; we
    // still defer actually using the dynamic table until both sides agree
    // (HandleSettings() flips enable_dynamic_table_).
    void RecomputeMaxTableCapacity() {
        if (peer_cap_known_) {
            max_table_capacity_ = local_max_table_capacity_ < peer_max_table_capacity_ ? local_max_table_capacity_
                                                                                       : peer_max_table_capacity_;
        } else {
            max_table_capacity_ = local_max_table_capacity_;
        }
        // BUGFIX: Keep the underlying DynamicTable's max_size in sync with the
        // negotiated effective capacity.  Without this, AddHeaderItem rejects
        // any entry whose size exceeds the *constructor-default* capacity
        // (1024 bytes) even when SETTINGS negotiated something larger, and
        // the caller (Encode / the decoder's instruction parser) would then
        // emit / accept a bogus reference to a non-existent entry.  RFC 9204 §3.2.3
        // requires the encoder/decoder dynamic tables to track the
        // negotiated cap.
        dynamic_table_.UpdateMaxTableSize(max_table_capacity_);
    }
    void EncodeString(const std::string& str, std::shared_ptr<common::IBuffer> buffer);

    // ==================== header-block encoding internals (RFC 9204 §4.5) ====================

    // Wire representation chosen for one header during Pass 1 of Encode().
    enum class EncodeAction {
        kStaticIndexed,           // Indexed Header Field — static table
        kStaticNameRef,           // Literal with name reference — static table
        kDynamicIndexed,          // Indexed Header Field — dynamic table (pre-base)
        kDynamicPostBaseIndexed,  // Indexed Header Field — dynamic table (post-base)
        kLiteralNoNameRef,        // Literal without name reference
    };
    struct HeaderEncoding {
        std::string name;
        std::string value;
        EncodeAction action;
        int32_t index;  // static or dynamic absolute index
    };

    // RFC 9114 §4.3: pseudo-headers first in fixed order (:method, :scheme,
    // :authority, :path, :status), then regular headers sorted alphabetically
    // so the encoding is deterministic regardless of unordered_map iteration
    // order.
    static std::vector<std::pair<std::string, std::string>> OrderHeaders(
        const std::vector<std::pair<std::string, std::string>>& headers);

    // Pass 1 of Encode(): pick the representation for one header — exact
    // static match, static name-ref, acknowledged dynamic entry, or literal.
    // May insert a new entry into the dynamic table (and announce it via
    // instruction_sender_) when the dynamic table is enabled. Raises
    // max_required_insert_count for every referenced dynamic entry so the
    // caller can derive the header block's Required Insert Count.
    HeaderEncoding DecideHeaderEncoding(const std::pair<std::string, std::string>& header, uint64_t base_insert_count,
        uint64_t& max_required_insert_count);

    // Pass 2 of Encode(): emit the wire bytes for one decision (RFC 9204
    // §4.5.2–§4.5.6). |base| is the block's Base, needed to relativise
    // dynamic-table absolute indexes.
    void WriteHeaderRepresentation(
        const HeaderEncoding& enc, int64_t base, const std::shared_ptr<common::IBuffer>& buffer);

    // ==================== encoder-instruction internals (RFC 9204 §4.3) ====================

    // Insert With Name Reference (1Sxxxxxx): static index if the name is in
    // the static table, dynamic relative index otherwise. Falls back to a
    // literal-name insert when the name is in neither table.
    bool EncodeInsertWithNameRef(
        const std::pair<std::string, std::string>& insert, const std::shared_ptr<common::IBuffer>& instr_buf);
    // Insert With Literal Name (01Hxxxxx): both name and value as string
    // literals, the name sharing the instruction byte's 6-bit length prefix.
    bool EncodeInsertWithLiteralName(
        const std::pair<std::string, std::string>& insert, const std::shared_ptr<common::IBuffer>& instr_buf);

private:
    DynamicTable dynamic_table_;
    // Effective cap = min(local, peer). Used by encoding paths.
    uint32_t max_table_capacity_;
    // Locally configured cap (set during connection Init from Http3Settings).
    uint32_t local_max_table_capacity_;
    // Peer's advertised SETTINGS_QPACK_MAX_TABLE_CAPACITY.
    uint32_t peer_max_table_capacity_;
    // True once peer SETTINGS for QPACK cap have been observed.
    bool peer_cap_known_;
    // Dynamic table enabled by default for better compression (RFC 9204)
    // Can be disabled via SetDynamicTableEnabled() if needed
    bool enable_dynamic_table_{true};
    std::function<void(const std::vector<std::pair<std::string, std::string>>&)> instruction_sender_;
    // Header blocks sent but not yet acknowledged / cancelled, oldest first per
    // stream. Only blocks that reference the dynamic table are recorded (a
    // block with Required Insert Count 0 is never acknowledged, RFC 9204 4.4.1).
    struct OutstandingSection {
        uint64_t required_insert_count;
        uint64_t min_referenced;  // lowest absolute index the block references
    };
    std::unordered_map<uint64_t, std::deque<OutstandingSection>> outstanding_;
    std::multiset<uint64_t> outstanding_min_refs_;  // one per outstanding section
    size_t outstanding_count_{0};
    // Lowest absolute index referenced by the block being encoded RIGHT NOW.
    // Pass 1 may insert (and therefore evict) between two of the block's own
    // references; the block's earlier references must already be protected.
    uint64_t inflight_block_min_ref_{UINT64_MAX};
    // Push min(outstanding sections, in-flight block) down to the table.
    void RefreshPinFloor();

    // P1-H4d: the decode-side state machine (header-block decoding, encoder-
    // instruction parsing, decoder feedback) moved to QpackDecoder. This class
    // keeps only the encoder role: field-section encoding, dynamic-table
    // inserts and capacity negotiation, outstanding-section tracking.
};

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_QPACK_QPACK_ENCODER
