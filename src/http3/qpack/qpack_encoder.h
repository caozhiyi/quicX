#ifndef HTTP3_QPACK_QPACK_ENCODER
#define HTTP3_QPACK_QPACK_ENCODER

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

#include "common/buffer/if_buffer.h"
#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

class QpackEncoder {
public:
    QpackEncoder(): dynamic_table_(1024), max_table_capacity_(1024),
        local_max_table_capacity_(1024), peer_max_table_capacity_(0),
        peer_cap_known_(false), enable_dynamic_table_(false) {}
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
    
    // Enable or disable dynamic table usage (default: enabled for better compression)
    void SetDynamicTableEnabled(bool enabled) { enable_dynamic_table_ = enabled; }
    bool IsDynamicTableEnabled() const { return enable_dynamic_table_; }

    bool Encode(const std::unordered_map<std::string, std::string>& headers, std::shared_ptr<common::IBuffer> buffer);
    bool Decode(const std::shared_ptr<common::IBuffer> buffer, std::unordered_map<std::string, std::string>& headers);
    // Returns the Required Insert Count of the most recently decoded header block.
    // RFC 9204 §4.4.1: the decoder MUST NOT emit Section Acknowledgment for header
    // blocks with a Required Insert Count of zero (no dependency on dynamic table).
    uint64_t GetLastDecodedRequiredInsertCount() const { return last_decoded_ric_; }
    // Set a callback used to send encoder instructions (Insert entries) on QPACK encoder stream
    void SetInstructionSender(std::function<void(const std::vector<std::pair<std::string,std::string>>&)> cb) { instruction_sender_ = std::move(cb); }
    // Optional: set a function to emit decoder stream frames (Section Ack, Stream Cancel, Insert Count Increment)
    void SetDecoderFeedbackSender(std::function<void(uint8_t type, uint64_t value)> cb) { decoder_feedback_sender_ = std::move(cb); }
    // Emit a decoder feedback frame via bound sender
    void EmitDecoderFeedback(uint8_t type, uint64_t value) { if (decoder_feedback_sender_) decoder_feedback_sender_(type, value); }
    // Decoder-stream side: parse QPACK encoder instructions (RFC 9204)
    bool DecodeEncoderInstructions(const std::shared_ptr<common::IBuffer> instr_buf);
    // Encoder-stream side: generate QPACK encoder instructions (RFC 9204)
    bool EncodeEncoderInstructions(const std::vector<std::pair<std::string,std::string>>& inserts,
                                   std::shared_ptr<common::IBuffer> instr_buf,
                                   bool with_name_ref = false,
                                   bool set_capacity = false,
                                   uint32_t new_capacity = 0,
                                   int32_t duplicate_index = -1);
    // HEADERS prefix write/read per RFC 9204
    void WriteHeaderPrefix(std::shared_ptr<common::IBuffer> buffer, uint64_t required_insert_count, int64_t base);
    bool ReadHeaderPrefix(const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, int64_t& base);

private:
    void SetEnableDynamicTable(bool enable) { enable_dynamic_table_ = enable; }
    // Recompute effective max_table_capacity_ from local + peer caps.
    // Before peer SETTINGS arrive we honour only the local cap so that
    // outbound encoding can proceed once SETTINGS have been observed; we
    // still defer actually using the dynamic table until both sides agree
    // (HandleSettings() flips enable_dynamic_table_).
    void RecomputeMaxTableCapacity() {
        if (peer_cap_known_) {
            max_table_capacity_ = local_max_table_capacity_ < peer_max_table_capacity_
                ? local_max_table_capacity_ : peer_max_table_capacity_;
        } else {
            max_table_capacity_ = local_max_table_capacity_;
        }
        // BUGFIX: Keep the underlying DynamicTable's max_size in sync with the
        // negotiated effective capacity.  Without this, AddHeaderItem rejects
        // any entry whose size exceeds the *constructor-default* capacity
        // (1024 bytes) even when SETTINGS negotiated something larger, and
        // the caller (Encode / DecodeEncoderInstructions) would then emit /
        // accept a bogus reference to a non-existent entry.  RFC 9204 §3.2.3
        // requires the encoder/decoder dynamic tables to track the
        // negotiated cap.
        dynamic_table_.UpdateMaxTableSize(max_table_capacity_);
    }
    // Write Required Insert Count and Base per RFC 9204 §4.5; here we set simple values for demo
    void WritePrefix(std::shared_ptr<common::IBuffer> buffer, uint64_t required_insert_count, uint64_t base);
    bool ReadPrefix(const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, uint64_t& base);
    void EncodeString(const std::string& str, std::shared_ptr<common::IBuffer> buffer);
    bool DecodeString(const std::shared_ptr<common::IBuffer> buffer, std::string& output);

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
    bool enable_dynamic_table_ {true};
    std::function<void(const std::vector<std::pair<std::string,std::string>>&)> instruction_sender_;
    std::function<void(uint8_t type, uint64_t value)> decoder_feedback_sender_;
    uint64_t last_decoded_ric_ {0};
};

}
}

#endif
