#ifndef HTTP3_QPACK_QPACK_DECODER
#define HTTP3_QPACK_QPACK_DECODER

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/buffer/if_buffer.h"
#include "http3/config.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "http3/qpack/dynamic_table.h"

namespace quicx {
namespace http3 {

/**
 * @brief QPACK decoder (P1-H4d)
 *
 * Owns the receive side of QPACK: parses the peer's encoder-stream
 * instructions (Insert / Set Capacity / Duplicate) into the local dynamic
 * table mirror, decodes field sections (header blocks) on request streams and
 * emits decoder feedback (Section Acknowledgment / Stream Cancellation) on
 * the decoder stream.
 *
 * Split out of the former dual-role QpackEncoder, which used to carry both
 * the encoder and the decoder state machine in one class distinguished only
 * by which receiver stream drove it.
 */
class QpackDecoder {
public:
    QpackDecoder() {}
    ~QpackDecoder() {}

    // P0-S6: why Decode returned false. Callers must distinguish: only
    // kBlocked may park the stream for a retry — malformed input and
    // oversized sections are terminal for the header block, and treating
    // them like "blocked" would retry forever.
    enum class DecodeStatus { kOk, kBlocked, kMalformed, kSectionTooLarge };
    DecodeStatus GetLastDecodeStatus() const { return last_decode_status_; }

    // -------- receive-side configuration (SETTINGS driven) --------

    /**
     * @brief Local dynamic table capacity this decoder advertised
     *
     * Upper bound for the peer's Set Dynamic Table Capacity instruction
     * (RFC 9204 §3.2.3) and MaxEntries basis for RIC reconstruction
     * (RFC 9204 §4.5.1.1).
     */
    void SetMaxTableCapacity(uint32_t capacity) {
        max_table_capacity_ = capacity;
        // Keep the mirror table in sync with the advertised capacity
        // (RFC 9204 §3.2.3): SetCapacity validation and the oversized-insert
        // rejection in DecodeEncoderInstructions key off the table's max_size.
        dynamic_table_.UpdateMaxTableSize(max_table_capacity_);
    }
    uint32_t GetMaxTableCapacity() const { return max_table_capacity_; }

    /**
     * @brief Enable/disable dynamic table usage for the decoder
     *
     * Mirrors the old dual-role API: the decoder side keeps accepting peer
     * encoder instructions regardless (it must, to keep its mirror
     * consistent); the flag records the negotiated preference.
     */
    void SetDynamicTableEnabled(bool enabled) { enable_dynamic_table_ = enabled; }
    bool IsDynamicTableEnabled() const { return enable_dynamic_table_; }

    /**
     * @brief SETTINGS_QPACK_BLOCKED_STREAMS-era field section size limit
     *
     * RFC 9114 §4.2.2: bounds the decompressed section size (per-field +32
     * overhead) DURING the decode, so a small reference cannot expand first.
     */
    void SetFieldSectionSizeLimit(uint64_t limit) { field_section_size_limit_ = limit; }

    // -------- decoder feedback sender (decoder stream) --------

    /**
     * @brief Bind the encoder-stream sender used to emit decoder feedback
     * @param sender Serializes Section Acknowledgment / Stream Cancellation /
     *               Insert Count Increment instructions onto the decoder stream
     */
    // Optional: set a function to emit decoder stream frames (Section Ack, Stream Cancel, Insert Count Increment)
    void SetDecoderFeedbackSender(std::function<void(uint8_t type, uint64_t value)> cb) {
        decoder_feedback_sender_ = std::move(cb);
    }

    // Emit a decoder feedback frame via bound sender
    void EmitDecoderFeedback(uint8_t type, uint64_t value) {
        if (decoder_feedback_sender_) decoder_feedback_sender_(type, value);
    }
    // P1-H5c (RFC 9204 §4.4.2): Stream Cancellation — tell the peer's encoder
    // that stream |stream_id| is abandoned so it stops tracking outstanding
    // section acknowledgments for it. The feedback sink takes the packed
    // header-block key ((stream_id << 32) | section_number) and puts only the
    // stream id on the wire.
    void CancelStreamFeedback(uint64_t stream_id) {
        EmitDecoderFeedback(
            static_cast<uint8_t>(QpackDecoderInstrType::kStreamCancellation), stream_id << 32);
    }

    // -------- field section decoding (request streams) --------

    /**
     * @brief Decode a field section (header block) from the request stream
     * @param buffer Wire bytes of the header block (RIC + Base + patterns)
     * @param headers Output vector of decoded field pairs
     * @return true on success; on failure check GetLastDecodeStatus()
     */
    bool Decode(const std::shared_ptr<common::IBuffer> buffer, std::vector<std::pair<std::string, std::string>>& headers);

    /**
     * @brief RIC of the most recently decoded section (drives Section Ack)
     */
    uint64_t GetLastDecodedRequiredInsertCount() const { return last_decoded_ric_; }

    /**
     * @brief Current insert count of the mirrored dynamic table
     *
     * QpackEncoderReceiverStream uses it to detect how many inserts the
     * freshly parsed instructions performed (RIC accounting).
     */
    uint64_t GetInsertCount() const { return dynamic_table_.GetInsertCount(); }

    // -------- peer encoder-stream instruction parsing --------

    /**
     * @brief Parse every complete instruction in |instr_buf|
     *
     * Streaming callers should prefer DecodeOneEncoderInstruction with
     * snapshot/commit semantics (P1-H5a).
     */
    bool DecodeEncoderInstructions(const std::shared_ptr<common::IBuffer> instr_buf);

    /**
     * @brief Parse exactly one instruction; false on malformed input
     */
    bool DecodeOneEncoderInstruction(const std::shared_ptr<common::IBuffer> instr_buf);

private:
    bool DecodeIndexedStatic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        std::vector<std::pair<std::string, std::string>>& headers);
    bool DecodeIndexedDynamic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers);
    bool DecodeLiteralNameRefStatic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        std::vector<std::pair<std::string, std::string>>& headers);
    bool DecodeLiteralNameRefDynamic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers);
    bool DecodeLiteralNoNameRef(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        std::vector<std::pair<std::string, std::string>>& headers);
    bool DecodePostBaseIndexed(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers);
    bool DecodePostBaseLiteralNameRef(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
        uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers);

    bool DecodeInstrInsertWithNameRef(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb);
    bool DecodeInstrInsertWithLiteralName(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb);
    bool DecodeInstrSetCapacity(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb);
    bool DecodeInstrDuplicate(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb);

    bool ReadHeaderPrefix(
        const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, int64_t& base);

    // Helper function to decode a string that may be Huffman encoded
    bool DecodeString(const std::shared_ptr<common::IBuffer> buffer, std::string& output);

    // Mirror of the peer encoder's table. Same construction default as the
    // old dual-role class used for its decoder half (1024 bytes until the
    // peer's Set Capacity instruction arrives, RFC 9204 §3.2.3).
    DynamicTable dynamic_table_{1024};

    std::function<void(uint8_t type, uint64_t value)> decoder_feedback_sender_;

    uint64_t last_decoded_ric_{0};  // RIC of the last decoded section
    // P0-S6: failure classification of the most recent Decode(). Defaults to
    // kOk like the old dual-role class.
    DecodeStatus last_decode_status_{DecodeStatus::kOk};
    // RFC 9114 §4.2.2 decompression-bomb bound; config fallback default.
    uint64_t field_section_size_limit_{kDefaultQpackDecodeSectionLimit};
    uint32_t max_table_capacity_{1024};  // advertised local capacity
    // Disabled until the connection layer enables it (if_connection flips it
    // on when SETTINGS advertise a non-zero QPACK table) — same start state
    // the old dual-role class had via its constructor init list.
    bool enable_dynamic_table_{false};
};

}  // namespace http3
}  // namespace quicx

#endif
