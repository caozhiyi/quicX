#include <algorithm>
#include <cstdint>

#include "common/log/log.h"

#include "http3/qpack/huffman_encoder.h"
#include "http3/qpack/qpack_constants.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/static_table.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

bool QpackEncoder::Encode(const std::vector<std::pair<std::string, std::string>>& headers,
    std::shared_ptr<common::IBuffer> buffer, uint64_t stream_id) {
    if (!buffer) {
        LOG_ERROR("QpackEncoder::Encode: buffer is null");
        return false;
    }

    // === Two-pass encoding strategy (RFC 9204) ===
    // Pass 1: Determine encoding decisions for each header, insert new entries
    //         into the dynamic table, and track the maximum required insert
    //         count (RIC).
    // Pass 2: Write the correct header prefix (RIC/Base) and encode header
    //         representations.

    // Base for this header block. We deliberately use the peer's Known
    // Received Count rather than our own total insert count: combined with the
    // "only reference acknowledged entries" rule in DecideHeaderEncoding() it
    // guarantees Base <= peer's applied insert count and Required Insert Count
    // <= Base, so this header block can never block a stream on the decoder
    // side (RFC 9204 §2.1.2). See QpackEncoder::OnPeerInsertCountIncrement().
    uint64_t base_insert_count = dynamic_table_.GetKnownReceivedCount();

    // Dynamic references are only allowed while the outstanding-section book can
    // take one more entry. A peer that never acknowledges would otherwise grow
    // it without bound; past the cap this degrades to literals (still correct,
    // just less compressed) until acknowledgments catch up.
    const bool tracked = (stream_id != kNoStreamId);
    const bool can_reference = !tracked || outstanding_count_ < kMaxOutstandingSections;
    const uint64_t reference_limit = can_reference ? base_insert_count : 0;

    // Pass 1: Determine encoding decisions
    std::vector<HeaderEncoding> encodings;
    uint64_t max_required_insert_count = 0;  // Track highest dynamic table entry referenced
    uint64_t min_referenced = UINT64_MAX;    // ... and the lowest

    inflight_block_min_ref_ = UINT64_MAX;
    RefreshPinFloor();
    for (const auto& h : OrderHeaders(headers)) {
        encodings.push_back(DecideHeaderEncoding(h, reference_limit, max_required_insert_count));
        const auto& enc = encodings.back();
        if (enc.action == EncodeAction::kDynamicIndexed) {
            min_referenced = std::min<uint64_t>(min_referenced, static_cast<uint64_t>(enc.index));
            // Protect it BEFORE the next header is decided: a later insert in
            // this same block must not evict what this block already points at.
            inflight_block_min_ref_ = min_referenced;
            RefreshPinFloor();
        }
    }
    inflight_block_min_ref_ = UINT64_MAX;

    // Pass 2: Compute correct RIC/Base and write everything
    uint64_t required_insert_count = max_required_insert_count;
    int64_t base = static_cast<int64_t>(base_insert_count);

    WriteHeaderPrefix(buffer, required_insert_count, base);

    for (const auto& enc : encodings) {
        WriteHeaderRepresentation(enc, base, buffer);
    }

    // From here the block is "sent" as far as the table is concerned: its
    // references stay protected until the peer acknowledges or cancels it.
    if (tracked && required_insert_count > 0 && min_referenced != UINT64_MAX) {
        outstanding_[stream_id].push_back(OutstandingSection{required_insert_count, min_referenced});
        outstanding_min_refs_.insert(min_referenced);
        ++outstanding_count_;
    }
    RefreshPinFloor();

    return true;
}

void QpackEncoder::RefreshPinFloor() {
    uint64_t floor = inflight_block_min_ref_;
    if (!outstanding_min_refs_.empty()) {
        floor = std::min<uint64_t>(floor, *outstanding_min_refs_.begin());
    }
    dynamic_table_.SetEvictionPinFloor(floor);
}

bool QpackEncoder::OnPeerSectionAck(uint64_t stream_id) {
    auto it = outstanding_.find(stream_id);
    if (it == outstanding_.end() || it->second.empty()) {
        LOG_WARN("QpackEncoder: Section Acknowledgment for stream %llu with no outstanding section, ignoring",
            (unsigned long long)stream_id);
        return false;
    }
    const OutstandingSection sec = it->second.front();
    it->second.pop_front();
    if (it->second.empty()) {
        outstanding_.erase(it);
    }
    auto mit = outstanding_min_refs_.find(sec.min_referenced);
    if (mit != outstanding_min_refs_.end()) {
        outstanding_min_refs_.erase(mit);
    }
    --outstanding_count_;

    // RFC 9204 2.1.4: acknowledging a section means the decoder applied every
    // insert up to its Required Insert Count.
    const uint64_t known = dynamic_table_.GetKnownReceivedCount();
    if (sec.required_insert_count > known) {
        dynamic_table_.AdvanceKnownReceived(sec.required_insert_count - known);
    }
    RefreshPinFloor();
    return true;
}

void QpackEncoder::OnPeerStreamCancellation(uint64_t stream_id) {
    auto it = outstanding_.find(stream_id);
    if (it == outstanding_.end()) {
        return;
    }
    for (const auto& sec : it->second) {
        auto mit = outstanding_min_refs_.find(sec.min_referenced);
        if (mit != outstanding_min_refs_.end()) {
            outstanding_min_refs_.erase(mit);
        }
        --outstanding_count_;
    }
    outstanding_.erase(it);
    RefreshPinFloor();
}

std::vector<std::pair<std::string, std::string>> QpackEncoder::OrderHeaders(
    const std::vector<std::pair<std::string, std::string>>& headers) {
    // RFC 9114 Section 4.3: Pseudo-headers MUST appear before regular headers
    // and MUST be in a specific order: :method, :scheme, :authority, :path (for
    // requests) or :status (for responses)
    static const std::vector<std::string> pseudo_header_order = {
        ":method", ":scheme", ":authority", ":path", ":status"};

    std::vector<std::pair<std::string, std::string>> ordered_headers;
    for (const auto& pseudo : pseudo_header_order) {
        for (const auto& header : headers) {
            if (header.first == pseudo) {
                // Duplicates are kept: RFC 9114 forbids repeated
                // pseudo-headers, but that is an HTTP-layer protocol error —
                // QPACK is a byte-faithful compression layer and must not
                // silently drop field lines (found by qpack_roundtrip_fuzz).
                ordered_headers.push_back(header);
            }
        }
    }
    // Regular headers: field-line order is significant (RFC 9110 §5.3 — a
    // proxy MUST NOT reorder same-named values), so emit them as received.
    // Sorting was only ever a workaround for unordered_map's random iteration
    // order and is no longer needed now that the caller supplies an ordered
    // sequence; keeping the original order also preserves duplicate values.
    // Unknown ':'-prefixed names are kept too: the known-pseudo-header set is
    // closed per RFC 9114 §4.3, but rejecting (or dropping — the old
    // behaviour, found by qpack_roundtrip_fuzz) them belongs to the HTTP
    // semantic layer, not to the compression layer.
    for (const auto& header : headers) {
        bool is_known_pseudo = false;
        for (const auto& pseudo : pseudo_header_order) {
            if (header.first == pseudo) {
                is_known_pseudo = true;
                break;
            }
        }
        if (is_known_pseudo) {
            continue;
        }
        ordered_headers.push_back(header);
    }
    return ordered_headers;
}

QpackEncoder::HeaderEncoding QpackEncoder::DecideHeaderEncoding(const std::pair<std::string, std::string>& header,
    uint64_t base_insert_count, uint64_t& max_required_insert_count) {
    HeaderEncoding enc;
    enc.name = header.first;
    enc.value = header.second;

    // Try exact match in static table
    int32_t index = StaticTable::Instance().FindHeaderItemIndex(header.first, header.second);
    if (index >= 0) {
        enc.action = EncodeAction::kStaticIndexed;
        enc.index = index;
        return enc;
    }

    // Try name-only match in the static table. NOTE (#9 fuzz finding): the
    // name is deliberately NOT lowercased before matching — the old
    // lowercasing silently rewrote field names on the wire (":methOd" was
    // encoded as a static name-ref and decoded back as ":method"). RFC 9114
    // §4.1.2 requires lowercase field names, but enforcing that is the HTTP
    // semantic layer's job; the QPACK layer must stay byte-faithful. A
    // mixed-case name simply misses the static table here and falls through
    // to a literal. Lowercase names (all legal traffic) are unaffected.
    index = StaticTable::Instance().FindHeaderItemIndex(header.first);
    if (index >= 0) {
        enc.action = EncodeAction::kStaticNameRef;
        enc.index = index;
        return enc;
    }

    // Try dynamic table if enabled.
    // RFC 9204 §3.2.3: "If the peer's SETTINGS_QPACK_MAX_TABLE_CAPACITY
    // value is 0, the encoder MUST NOT use the dynamic table." We honour
    // this by gating on the *effective* cap = min(local, peer).  Even
    // though connection_client/server flips enable_dynamic_table_ based
    // on the *local* configuration alone, the peer can still advertise
    // 0 via SETTINGS at run time and we must stop inserting immediately.
    if (enable_dynamic_table_ && max_table_capacity_ > 0) {
        // BUGFIX P1-1: Use absolute index instead of deque position.
        // FindAbsoluteIndex returns RFC 9204 absolute index, which is
        // monotonically increasing and survives evictions.
        int64_t abs_idx = dynamic_table_.FindAbsoluteIndex(header.first, header.second);
        if (abs_idx >= 0) {
            uint64_t abs_idx_u = static_cast<uint64_t>(abs_idx);
            // Only entries the peer has already applied may be referenced.
            // Referencing a newer entry would push Required Insert Count
            // past the peer's applied count and block the stream, which
            // overruns SETTINGS_QPACK_BLOCKED_STREAMS and gets the whole
            // connection killed with QPACK_DECOMPRESSION_FAILED.
            if (abs_idx_u < base_insert_count) {
                enc.action = EncodeAction::kDynamicIndexed;
                enc.index = static_cast<int32_t>(abs_idx);
                max_required_insert_count = std::max(max_required_insert_count, abs_idx_u + 1);
                return enc;
            }
            // Entry exists but is not acknowledged yet: emit it literally
            // this time. Do NOT insert a duplicate — the entry is already
            // in our table and will become referenceable once the peer
            // acknowledges it.
            enc.action = EncodeAction::kLiteralNoNameRef;
            enc.index = -1;
            return enc;
        }

        // Insert new entry into dynamic table.  AddHeaderItem rejects
        // entries that on their own exceed max_size_ (RFC 7541 §4.4 —
        // "an entry larger than the maximum size MUST NOT be added").
        // In that case we fall through to the literal-without-name-ref
        // path; we MUST NOT pretend the insert succeeded, since
        // doing so would emit an Insert instruction the peer never
        // applies AND a post-base reference to a non-existent entry,
        // which corrupts the wire format (observed as a varint overflow
        // on the decoder side).
        //
        // P1-H4c (RFC 9204 §3.2.4 rule 2): likewise refuse the insert when
        // it would evict an entry the peer has not yet acknowledged (its
        // absolute insert number >= Known Received Count): the peer's
        // table must not drift from ours. Literal-encode this header; a
        // later attempt (after the Insert Count Increment arrives) will
        // free the eviction headroom and reference normally.
        if (dynamic_table_.WouldEvictProtectedEntry(
                static_cast<uint32_t>(header.first.size() + header.second.size() + 32))) {
            enc.action = EncodeAction::kLiteralNoNameRef;
            enc.index = -1;
            return enc;
        }
        if (dynamic_table_.AddHeaderItem(header.first, header.second)) {
            if (instruction_sender_) {
                instruction_sender_({{header.first, header.second}});
            }

            // The insert has only just been announced on the encoder stream,
            // so the peer has certainly not applied it yet. Emitting a
            // post-base reference here (the previous behaviour) made the
            // Required Insert Count exceed the peer's applied count for EVERY
            // header block that introduced a new entry, blocking one stream
            // per request. Encode the value literally this time; once the
            // peer's Insert Count Increment arrives, the branch above will
            // reference it.
        }
        enc.action = EncodeAction::kLiteralNoNameRef;
        enc.index = -1;
        return enc;
    }

    // Fall through to literal without name reference
    enc.action = EncodeAction::kLiteralNoNameRef;
    enc.index = -1;
    return enc;
}

void QpackEncoder::WriteHeaderRepresentation(
    const HeaderEncoding& enc, int64_t base, const std::shared_ptr<common::IBuffer>& buffer) {
    switch (enc.action) {
        case EncodeAction::kStaticIndexed: {
            QpackEncodePrefixedInteger(buffer, QpackHeaderPattern::kIndexedStaticPrefix,
                QpackHeaderPattern::kIndexedStatic, static_cast<uint64_t>(enc.index));
            break;
        }
        case EncodeAction::kStaticNameRef: {
            QpackEncodePrefixedInteger(buffer, QpackHeaderPattern::kLiteralNameRefStaticPrefix,
                QpackHeaderPattern::kLiteralNameRefStatic, static_cast<uint64_t>(enc.index));
            EncodeString(enc.value, buffer);
            break;
        }
        case EncodeAction::kDynamicIndexed: {
            // Pre-base reference: relative_index = base - 1 - absolute_index
            uint64_t relative = static_cast<uint64_t>(base - 1 - enc.index);
            QpackEncodePrefixedInteger(
                buffer, QpackHeaderPattern::kIndexedDynamicPrefix, QpackHeaderPattern::kIndexedDynamic, relative);
            break;
        }
        case EncodeAction::kDynamicPostBaseIndexed: {
            // Post-base reference: post_base_index = absolute_index - base
            uint64_t post_base_index = static_cast<uint64_t>(enc.index - static_cast<int32_t>(base));
            QpackEncodePrefixedInteger(buffer, QpackHeaderPattern::kPostBaseIndexedPrefix,
                QpackHeaderPattern::kPostBaseIndexed, post_base_index);
            break;
        }
        case EncodeAction::kLiteralNoNameRef: {
            // RFC 9204 Section 4.5.6: Literal Header Field Without Name Reference (001xxxxx)
            bool use_huffman = HuffmanEncoder::Instance().ShouldHuffmanEncode(enc.name);
            uint8_t first_byte = QpackHeaderPattern::kLiteralNoNameRef;
            if (use_huffman) {
                first_byte |= 0x08;  // H bit
            }

            std::string name_to_encode = enc.name;
            if (use_huffman) {
                std::vector<uint8_t> encoded = HuffmanEncoder::Instance().Encode(enc.name);
                name_to_encode.assign(reinterpret_cast<char*>(encoded.data()), encoded.size());
            }

            QpackEncodePrefixedInteger(buffer, 3, first_byte, static_cast<uint64_t>(name_to_encode.length()));
            buffer->Write(reinterpret_cast<const uint8_t*>(name_to_encode.data()), name_to_encode.length());

            EncodeString(enc.value, buffer);
            break;
        }
    }
}

bool QpackEncoder::EncodeEncoderInstructions(const std::vector<std::pair<std::string, std::string>>& inserts,
    std::shared_ptr<common::IBuffer> instr_buf, bool with_name_ref, bool set_capacity, uint32_t new_capacity,
    int32_t duplicate_index) {
    if (!instr_buf) {
        LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: instr_buf is null");
        return false;
    }

    if (set_capacity) {
        // Set Dynamic Table Capacity (001xxxxx)
        if (!QpackEncodePrefixedInteger(instr_buf, QpackEncoderInstr::kSetDynamicTableCapacityPrefix,
                QpackEncoderInstr::kSetDynamicTableCapacity, new_capacity)) {
            LOG_ERROR(
                "QpackEncoder::EncodeEncoderInstructions: encode set dynamic table capacity failed. new_capacity:%u",
                new_capacity);
            return false;
        }
    }

    if (duplicate_index >= 0) {
        // Duplicate (0001xxxx)
        if (!QpackEncodePrefixedInteger(instr_buf, QpackEncoderInstr::kDuplicatePrefix, QpackEncoderInstr::kDuplicate,
                static_cast<uint64_t>(duplicate_index))) {
            LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode duplicate failed. duplicate_index:%d",
                duplicate_index);
            return false;
        }
    }

    for (const auto& p : inserts) {
        if (with_name_ref) {
            if (!EncodeInsertWithNameRef(p, instr_buf)) {
                return false;
            }
        } else {
            if (!EncodeInsertWithLiteralName(p, instr_buf)) {
                return false;
            }
        }
    }
    return true;
}

bool QpackEncoder::EncodeInsertWithNameRef(
    const std::pair<std::string, std::string>& insert, const std::shared_ptr<common::IBuffer>& instr_buf) {
    // Insert With Name Reference (1Sxxxxxx)
    // S=1 static, S=0 dynamic; 6-bit prefix for index
    const std::string& name = insert.first;
    const std::string& value = insert.second;

    int32_t s_name_idx = StaticTable::Instance().FindHeaderItemIndex(name);
    if (s_name_idx >= 0) {
        // S=1, encode static index
        uint8_t mask = QpackEncoderInstr::kInsertWithNameRef | QpackEncoderInstr::kInsertWithNameRefStaticBit;
        if (!QpackEncodePrefixedInteger(
                instr_buf, QpackEncoderInstr::kInsertWithNameRefPrefix, mask, static_cast<uint64_t>(s_name_idx))) {
            LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode insert with name ref failed. s_name_idx:%d",
                s_name_idx);
            return false;
        }
    } else {
        // S=0, dynamic name index relative to current Insert Count (absolute index to relative per RFC 9204)
        // BUGFIX P1-1: Use GetInsertCount() instead of GetEntryCount()
        uint64_t ric = dynamic_table_.GetInsertCount();
        int64_t d_name_idx = dynamic_table_.FindAbsoluteNameIndex(name);
        if (d_name_idx < 0) {
            // Name not in either table: fall back to Insert With Literal Name.
            return EncodeInsertWithLiteralName(insert, instr_buf);
        }
        // dynamic relative index = ric - 1 - absolute_index
        // d_name_idx is an absolute index from FindAbsoluteNameIndex
        uint64_t relative = static_cast<uint64_t>(static_cast<int64_t>(ric) - 1 - d_name_idx);
        if (!QpackEncodePrefixedInteger(instr_buf, QpackEncoderInstr::kInsertWithNameRefPrefix,
                QpackEncoderInstr::kInsertWithNameRef, relative)) {
            LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode insert with name ref failed. relative:%llu",
                relative);
            return false;
        }
    }

    if (!QpackEncodeStringLiteral(value, instr_buf, false)) {
        LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. value:%s", value.c_str());
        return false;
    }
    return true;
}

bool QpackEncoder::EncodeInsertWithLiteralName(
    const std::pair<std::string, std::string>& insert, const std::shared_ptr<common::IBuffer>& instr_buf) {
    // Insert With Literal Name (01Hxxxxx): the name is a 6-bit prefix
    // string literal packed into the instruction byte itself.
    if (!QpackEncodeStringLiteralWithPrefix(insert.first, instr_buf, QpackEncoderInstr::kInsertWithoutNameRefPrefix,
            QpackEncoderInstr::kInsertWithoutNameRef, QpackEncoderInstr::kInsertWithoutNameRefHuffmanBit, false)) {
        LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. name:%s",
            insert.first.c_str());
        return false;
    }
    if (!QpackEncodeStringLiteral(insert.second, instr_buf, false)) {
        LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. value:%s",
            insert.second.c_str());
        return false;
    }
    return true;
}

void QpackEncoder::WriteHeaderPrefix(
    std::shared_ptr<common::IBuffer> buffer, uint64_t required_insert_count, int64_t base) {
    // RFC 9204 Section 4.5.1: Encode Required Insert Count and Delta Base

    // RFC 9204 Section 4.5.1.1: Encoded Required Insert Count
    // MaxEntries = floor(MaxTableCapacity / 32)
    // EncodedRIC = (RIC % (2 * MaxEntries)) + 1  (when RIC > 0)
    // EncodedRIC = 0                               (when RIC == 0)
    uint64_t encoded_ric = 0;
    if (required_insert_count > 0) {
        uint64_t max_entries = max_table_capacity_ / 32;
        if (max_entries == 0) {
            max_entries = 1;
        }
        encoded_ric = (required_insert_count % (2 * max_entries)) + 1;
    }
    QpackEncodePrefixedInteger(buffer, QpackHeaderPrefix::kRequiredInsertCountPrefix, 0x00, encoded_ric);

    // Encode Delta Base (7-bit prefix with S bit), RFC 9204 Section 4.5.1:
    //   Base >= ReqInsertCount : S = 0, DeltaBase = Base - ReqInsertCount
    //   Base <  ReqInsertCount : S = 1, DeltaBase = ReqInsertCount - Base - 1
    // The "- 1" in the S=1 branch is mandatory: the sign bit already tells the
    // decoder the direction, so the value 0 is reused for a delta of one.
    // Omitting it makes every post-base reference off by one against a
    // conforming peer (while staying self-consistent, hiding the bug in
    // encoder<->decoder round-trip tests).
    int64_t delta_base = base - static_cast<int64_t>(required_insert_count);
    bool s_bit = (delta_base < 0);
    uint64_t abs_delta_base = static_cast<uint64_t>(s_bit ? (-delta_base - 1) : delta_base);
    uint8_t s_mask = s_bit ? QpackHeaderPrefix::kDeltaBaseSignBit : 0x00;
    QpackEncodePrefixedInteger(buffer, QpackHeaderPrefix::kDeltaBasePrefix, s_mask, abs_delta_base);
}

// Helper function to encode a string with Huffman encoding if beneficial
void QpackEncoder::EncodeString(const std::string& str, std::shared_ptr<common::IBuffer> buffer) {
    if (HuffmanEncoder::Instance().ShouldHuffmanEncode(str)) {
        // Encode with Huffman
        std::vector<uint8_t> encoded = HuffmanEncoder::Instance().Encode(str);

        // Write length prefix with H bit set (7-bit prefix)
        // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
        QpackEncodePrefixedInteger(
            buffer, QpackString::kLengthPrefix, QpackString::kHuffmanBit, static_cast<uint64_t>(encoded.size()));

        // Write Huffman-encoded string
        buffer->Write(encoded.data(), encoded.size());
    } else {
        // Write length prefix without H bit (7-bit prefix)
        // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
        QpackEncodePrefixedInteger(buffer, QpackString::kLengthPrefix, 0x00, static_cast<uint64_t>(str.length()));

        // Write string directly
        if (!str.empty()) {
            buffer->Write((uint8_t*)str.data(), str.length());
        }
    }
}

// Helper function to decode a string that may be Huffman encoded

}  // namespace http3
}  // namespace quicx
