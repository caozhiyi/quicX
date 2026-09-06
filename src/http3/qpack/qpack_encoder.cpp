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

bool QpackEncoder::Encode(
    const std::unordered_map<std::string, std::string>& headers, std::shared_ptr<common::IBuffer> buffer) {
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
    uint64_t base_insert_count = known_received_count_;

    // Pass 1: Determine encoding decisions
    std::vector<HeaderEncoding> encodings;
    uint64_t max_required_insert_count = 0;  // Track highest dynamic table entry referenced

    for (const auto& h : OrderHeaders(headers)) {
        encodings.push_back(DecideHeaderEncoding(h, base_insert_count, max_required_insert_count));
    }

    // Pass 2: Compute correct RIC/Base and write everything
    uint64_t required_insert_count = max_required_insert_count;
    int64_t base = static_cast<int64_t>(base_insert_count);

    WriteHeaderPrefix(buffer, required_insert_count, base);

    for (const auto& enc : encodings) {
        WriteHeaderRepresentation(enc, base, buffer);
    }

    return true;
}

std::vector<std::pair<std::string, std::string>> QpackEncoder::OrderHeaders(
    const std::unordered_map<std::string, std::string>& headers) {
    // RFC 9114 Section 4.3: Pseudo-headers MUST appear before regular headers
    // and MUST be in a specific order: :method, :scheme, :authority, :path (for
    // requests) or :status (for responses)
    static const std::vector<std::string> pseudo_header_order = {
        ":method", ":scheme", ":authority", ":path", ":status"};

    std::vector<std::pair<std::string, std::string>> ordered_headers;
    for (const auto& pseudo : pseudo_header_order) {
        auto it = headers.find(pseudo);
        if (it != headers.end()) {
            ordered_headers.push_back({it->first, it->second});
        }
    }
    // Regular headers sorted for deterministic output (fixes P2-3: unordered_map iteration order)
    std::vector<std::pair<std::string, std::string>> regular_headers;
    for (const auto& header : headers) {
        if (!header.first.empty() && header.first[0] == ':') {
            continue;
        }
        regular_headers.push_back({header.first, header.second});
    }
    std::sort(regular_headers.begin(), regular_headers.end());
    ordered_headers.insert(ordered_headers.end(), regular_headers.begin(), regular_headers.end());
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

    // Try name-only match in static table (with lowercase)
    std::string lower_name = header.first;
    for (auto& c : lower_name) c = std::tolower(static_cast<unsigned char>(c));
    index = StaticTable::Instance().FindHeaderItemIndex(lower_name);
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

bool QpackEncoder::Decode(
    const std::shared_ptr<common::IBuffer> buffer, std::unordered_map<std::string, std::string>& headers) {
    if (!buffer || buffer->GetDataLength() < 2) {
        LOG_ERROR("QpackEncoder::Decode: buffer is null or data length is less than 2");
        return false;
    }

    // Read Required Insert Count and Base
    uint64_t required_insert_count = 0;
    int64_t base = 0;
    if (!ReadHeaderPrefix(buffer, required_insert_count, base)) {
        LOG_ERROR("QpackEncoder::Decode: read header prefix failed");
        return false;
    }
    // Remember for callers that need to decide whether to emit Section Ack
    // (RFC 9204 §4.4.1: MUST NOT emit Section Ack when RIC is zero).
    last_decoded_ric_ = required_insert_count;
    // RFC 9204 §2.1.4: Required Insert Count is an *absolute* index threshold —
    // the number of entries the decoder MUST have already inserted in order
    // to safely decode this header block.  It is therefore compared against
    // the monotonic insert count (GetInsertCount()), NOT the post-eviction
    // GetEntryCount().  After evictions, GetEntryCount() < GetInsertCount(),
    // and using the smaller value here causes the decoder to spuriously
    // reject perfectly valid header blocks that reference a current,
    // not-yet-evicted entry whose absolute index simply happens to exceed
    // the deque size.
    if (required_insert_count > dynamic_table_.GetInsertCount()) {
        // Signal blocked; in full RFC flow, should queue this header block and return
        LOG_ERROR(
            "QpackEncoder::Decode: required insert count is greater than current inserted "
            "count.required_insert_count:%llu, current_inserted_count:%llu",
            required_insert_count, dynamic_table_.GetInsertCount());
        return false;
    }

    // Dispatch loop: each wire pattern (RFC 9204 §4.5) decodes into its own
    // method; this loop only consumes the pattern byte and routes.
    while (buffer->GetDataLength() > 0) {
        uint8_t first_byte;
        buffer->Read(&first_byte, 1);

        bool ok = false;
        if ((first_byte & QpackHeaderPattern::kIndexedStaticMask) == QpackHeaderPattern::kIndexedStatic) {
            ok = DecodeIndexedStatic(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kIndexedDynamicMask) == QpackHeaderPattern::kIndexedDynamic) {
            ok = DecodeIndexedDynamic(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kLiteralNameRefStaticMask) ==
                   QpackHeaderPattern::kLiteralNameRefStatic) {
            ok = DecodeLiteralNameRefStatic(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kLiteralNameRefDynamicMask) ==
                   QpackHeaderPattern::kLiteralNameRefDynamic) {
            ok = DecodeLiteralNameRefDynamic(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kLiteralNoNameRefMask) == QpackHeaderPattern::kLiteralNoNameRef) {
            ok = DecodeLiteralNoNameRef(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kPostBaseIndexedMask) == QpackHeaderPattern::kPostBaseIndexed) {
            ok = DecodePostBaseIndexed(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kPostBaseLiteralNameRefMask) ==
                   QpackHeaderPattern::kPostBaseLiteralNameRef) {
            ok = DecodePostBaseLiteralNameRef(buffer, first_byte, base, headers);
        } else {
            LOG_ERROR("QpackEncoder::Decode: unknown header pattern. first_byte:%d", first_byte);
            return false;
        }
        if (!ok) {
            return false;
        }
    }

    return true;
}

bool QpackEncoder::DecodeIndexedStatic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t /*base*/, std::unordered_map<std::string, std::string>& headers) {
    // Indexed — static (11xxxxxx)
    uint64_t sidx = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kIndexedStaticPrefix, first_byte, sidx)) {
        LOG_ERROR("QpackEncoder::Decode: decode indexed static failed. sidx:%llu", sidx);
        return false;
    }
    auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
    if (!item) {
        LOG_ERROR("QpackEncoder::Decode: find header item failed. sidx:%llu", sidx);
        return false;
    }
    headers[item->name_] = item->value_;
    return true;
}

bool QpackEncoder::DecodeIndexedDynamic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
    std::unordered_map<std::string, std::string>& headers) {
    // Indexed — dynamic (10xxxxxx)
    uint64_t rel = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kIndexedDynamicPrefix, first_byte, rel)) {
        LOG_ERROR("QpackEncoder::Decode: decode indexed dynamic failed. rel:%llu", rel);
        return false;
    }
    int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
    if (abs_index < 0) {
        LOG_ERROR("QpackEncoder::Decode: absolute index is less than 0. abs_index:%lld", abs_index);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    headers[item->name_] = item->value_;
    return true;
}

bool QpackEncoder::DecodeLiteralNameRefStatic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t /*base*/, std::unordered_map<std::string, std::string>& headers) {
    // Literal with name reference — static (0101xxxx, T=1)
    uint64_t sidx = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kLiteralNameRefStaticPrefix, first_byte, sidx)) {
        LOG_ERROR("QpackEncoder::Decode: decode literal name ref static failed. sidx:%llu", sidx);
        return false;
    }
    auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
    if (!item) {
        LOG_ERROR("QpackEncoder::Decode: find header item failed. sidx:%llu", sidx);
        return false;
    }
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackEncoder::Decode: decode string failed. name:%s, value:%s", item->name_.c_str(),
            value.c_str());
        return false;
    }
    headers[item->name_] = value;
    return true;
}

bool QpackEncoder::DecodeLiteralNameRefDynamic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t base, std::unordered_map<std::string, std::string>& headers) {
    // Literal with name reference — dynamic (010xxxxx)
    uint64_t rel = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kLiteralNameRefDynamicPrefix, first_byte, rel)) {
        LOG_ERROR("QpackEncoder::Decode: decode literal name ref dynamic failed. rel:%llu", rel);
        return false;
    }
    int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
    if (abs_index < 0) {
        LOG_ERROR("QpackEncoder::Decode: absolute index is less than 0. abs_index:%lld", abs_index);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackEncoder::Decode: decode string failed. value:%s", value.c_str());
        return false;
    }
    headers[item->name_] = value;
    return true;
}

bool QpackEncoder::DecodeLiteralNoNameRef(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t /*base*/, std::unordered_map<std::string, std::string>& headers) {
    // RFC 9204 Section 4.5.6: Literal Field Line With Literal Name (001xxxxx)
    // Format: 001 N H NameLen(3+) | Name | H ValueLen(7+) | Value
    // first_byte already consumed, contains N, H bits and 3-bit name length prefix
    bool name_huffman = (first_byte & 0x08) != 0;  // H bit for name (bit 3)
    uint64_t name_len = 0;
    // Name length uses 3-bit prefix (bits 0-2)
    if (!QpackDecodePrefixedIntegerFrom(buffer, 3, first_byte, name_len)) {
        LOG_ERROR("QpackEncoder::Decode: decode name length failed for LiteralNoNameRef");
        return false;
    }

    // Read name string
    std::string name;
    if (name_len > 0) {
        if (name_huffman) {
            std::vector<uint8_t> encoded;
            encoded.resize(static_cast<size_t>(name_len));
            if (buffer->Read(encoded.data(), static_cast<uint32_t>(name_len)) != static_cast<uint32_t>(name_len)) {
                LOG_ERROR("QpackEncoder::Decode: read huffman name failed. len:%llu", name_len);
                return false;
            }
            name = HuffmanEncoder::Instance().Decode(encoded);
        } else {
            name.resize(static_cast<size_t>(name_len));
            if (buffer->Read((uint8_t*)name.data(), static_cast<uint32_t>(name_len)) !=
                static_cast<uint32_t>(name_len)) {
                LOG_ERROR("QpackEncoder::Decode: read name failed. len:%llu", name_len);
                return false;
            }
        }
    }

    // Read value using DecodeString (7-bit prefix with H bit)
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackEncoder::Decode: decode value string failed. name:%s", name.c_str());
        return false;
    }

    LOG_DEBUG("QpackEncoder::Decode: LiteralNoNameRef decoded header: %s=%s, remaining=%u", name.c_str(),
        value.c_str(), buffer->GetDataLength());
    headers[name] = value;
    return true;
}

bool QpackEncoder::DecodePostBaseIndexed(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t base, std::unordered_map<std::string, std::string>& headers) {
    // RFC 9204 Section 4.5.3: Post-Base Indexed Header Field (0001xxxx)
    // Index is relative to Base, references entries inserted AFTER Base
    uint64_t post_base_index = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kPostBaseIndexedPrefix, first_byte,
            post_base_index)) {
        LOG_ERROR("QpackEncoder::Decode: decode post base indexed failed. post_base_index:%llu", post_base_index);
        return false;
    }
    // Convert to absolute index: abs_index = Base + post_base_index
    int64_t abs_index = base + static_cast<int64_t>(post_base_index);
    // Upper bound is the monotonic insert count, not the post-eviction
    // deque size: a valid absolute index lives in [evicted_count,
    // insert_count); FindHeaderItemByAbsoluteIndex below catches the
    // lower bound (returns nullptr on already-evicted entries).
    if (abs_index < 0 || abs_index >= static_cast<int64_t>(dynamic_table_.GetInsertCount())) {
        LOG_ERROR("QpackEncoder::Decode: absolute index is out of range. abs_index:%lld", abs_index);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    headers[item->name_] = item->value_;
    return true;
}

bool QpackEncoder::DecodePostBaseLiteralNameRef(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t base, std::unordered_map<std::string, std::string>& headers) {
    // RFC 9204 Section 4.5.5: Literal Header Field With Post-Base Name Reference (0000xxxx)
    // Name reference is relative to Base, value is literal
    uint64_t post_base_index = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kPostBaseLiteralNameRefPrefix, first_byte,
            post_base_index)) {
        LOG_ERROR("QpackEncoder::Decode: decode post base literal name ref failed. post_base_index:%llu",
            post_base_index);
        return false;
    }
    // Convert to absolute index: abs_index = Base + post_base_index
    int64_t abs_index = base + static_cast<int64_t>(post_base_index);
    // Same bound choice as DecodePostBaseIndexed (see note there).
    if (abs_index < 0 || abs_index >= static_cast<int64_t>(dynamic_table_.GetInsertCount())) {
        LOG_ERROR("QpackEncoder::Decode: absolute index is out of range. abs_index:%lld", abs_index);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackEncoder::Decode: decode string failed. value:%s", value.c_str());
        return false;
    }
    headers[item->name_] = value;
    return true;
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

bool QpackEncoder::DecodeEncoderInstructions(const std::shared_ptr<common::IBuffer> instr_buf) {
    if (!instr_buf) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: instr_buf is null");
        return false;
    }

    // Dispatch loop: each instruction pattern (RFC 9204 §4.3) decodes into
    // its own method; this loop only consumes the pattern byte and routes.
    while (instr_buf->GetDataLength() > 0) {
        uint8_t fb = 0;
        if (instr_buf->Read(&fb, 1) != 1) {
            LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: read byte failed. fb:%d", fb);
            return false;
        }

        bool ok = false;
        if (fb & QpackEncoderInstr::kInsertWithNameRefMask) {
            ok = DecodeInstrInsertWithNameRef(instr_buf, fb);
        } else if ((fb & QpackEncoderInstr::kInsertWithoutNameRefMask) == QpackEncoderInstr::kInsertWithoutNameRef) {
            ok = DecodeInstrInsertWithLiteralName(instr_buf, fb);
        } else if ((fb & QpackEncoderInstr::kSetDynamicTableCapacityMask) ==
                   QpackEncoderInstr::kSetDynamicTableCapacity) {
            ok = DecodeInstrSetCapacity(instr_buf, fb);
        } else if ((fb & QpackEncoderInstr::kDuplicateMask) == QpackEncoderInstr::kDuplicate) {
            ok = DecodeInstrDuplicate(instr_buf, fb);
        } else {
            LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: unknown instruction. fb:%d", fb);
            break;
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool QpackEncoder::DecodeInstrInsertWithNameRef(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // Insert With Name Reference (1Sxxxxxx)
    uint64_t idx = 0;
    if (!QpackDecodePrefixedIntegerFrom(instr_buf, QpackEncoderInstr::kInsertWithNameRefPrefix, fb, idx)) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode insert with name ref failed. idx:%llu", idx);
        return false;
    }
    bool is_static = (fb & QpackEncoderInstr::kInsertWithNameRefStaticBit) != 0;  // S bit
    std::string value;
    if (!QpackDecodeStringLiteral(instr_buf, value)) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode string literal failed. value:%s", value.c_str());
        return false;
    }
    std::string name;
    if (is_static) {
        auto hi = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(idx));
        if (!hi) {
            LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: find header item failed. idx:%llu", idx);
            return false;
        }
        name = hi->name_;
    } else {
        // dynamic: idx is relative to current insert count
        // RFC 9204: relative_index = insert_count - 1 - absolute_index
        // So: absolute_index = insert_count - 1 - relative_index
        uint64_t insert_count = dynamic_table_.GetInsertCount();
        int64_t abs = static_cast<int64_t>(insert_count) - 1 - static_cast<int64_t>(idx);
        if (abs < 0) {
            LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: absolute index is less than 0. abs:%lld", abs);
            return false;
        }
        auto hi = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs));
        if (!hi) {
            LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: find header item failed. abs:%lld", abs);
            return false;
        }
        name = hi->name_;
    }
    // RFC 9204 §3.2.3 / RFC 7541 §4.4: An Insert instruction whose
    // entry alone exceeds the dynamic table capacity is a protocol
    // error on the encoder stream — we must NOT silently accept it
    // (the peer's encoder believes the entry exists at the next
    // absolute index, so any subsequent reference would be invalid).
    if (!dynamic_table_.AddHeaderItem(name, value)) {
        LOG_ERROR(
            "QpackEncoder::DecodeEncoderInstructions: AddHeaderItem rejected oversized entry "
            "(name_len=%zu value_len=%zu max_size=%u)",
            name.size(), value.size(), max_table_capacity_);
        return false;
    }
    return true;
}

bool QpackEncoder::DecodeInstrInsertWithLiteralName(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // Insert With Literal Name (01Hxxxxx), RFC 9204 Section 4.3.3.
    // The name length lives in this very byte, so it must be decoded
    // from |fb| rather than from a fresh 8-bit prefix string literal.
    std::string name, value;
    if (!QpackDecodeStringLiteralWithPrefix(instr_buf, fb, QpackEncoderInstr::kInsertWithoutNameRefPrefix,
            QpackEncoderInstr::kInsertWithoutNameRefHuffmanBit, name)) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode string literal failed. name:%s", name.c_str());
        return false;
    }
    if (!QpackDecodeStringLiteral(instr_buf, value)) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode string literal failed. value:%s", value.c_str());
        return false;
    }
    // See note in DecodeInstrInsertWithNameRef: oversized inserts must be a
    // protocol error.
    if (!dynamic_table_.AddHeaderItem(name, value)) {
        LOG_ERROR(
            "QpackEncoder::DecodeEncoderInstructions: AddHeaderItem rejected oversized entry "
            "(name_len=%zu value_len=%zu max_size=%u)",
            name.size(), value.size(), max_table_capacity_);
        return false;
    }
    return true;
}

bool QpackEncoder::DecodeInstrSetCapacity(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // RFC 9204 Section 4.3.1: Set Dynamic Table Capacity (001xxxxx)
    uint64_t cap = 0;
    if (!QpackDecodePrefixedIntegerFrom(instr_buf, QpackEncoderInstr::kSetDynamicTableCapacityPrefix, fb, cap)) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode set dynamic table capacity failed. cap:%llu", cap);
        return false;
    }

    // RFC 9204 Section 3.2.3: Validate capacity against SETTINGS_QPACK_MAX_TABLE_CAPACITY
    if (cap > max_table_capacity_) {
        // Decoder MUST treat this as a connection error (QPACK_ENCODER_STREAM_ERROR)
        LOG_ERROR(
            "QpackEncoder::DecodeEncoderInstructions: capacity is greater than max table capacity. cap:%llu, "
            "max_table_capacity:%u",
            cap, max_table_capacity_);
        return false;
    }

    dynamic_table_.UpdateMaxTableSize(static_cast<uint32_t>(cap));
    return true;
}

bool QpackEncoder::DecodeInstrDuplicate(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // RFC 9204 Section 4.3.4: Duplicate instruction (000xxxxx)
    uint64_t rel = 0;
    if (!QpackDecodePrefixedIntegerFrom(instr_buf, QpackEncoderInstr::kDuplicatePrefix, fb, rel)) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode duplicate failed. rel:%llu", rel);
        return false;
    }
    uint64_t insert_count = dynamic_table_.GetInsertCount();
    int64_t abs = static_cast<int64_t>(insert_count) - 1 - static_cast<int64_t>(rel);
    if (abs < 0) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: absolute index is less than 0. abs:%lld", abs);
        return false;
    }
    // Check if entry has been evicted
    uint64_t evicted = dynamic_table_.GetInsertCount() - dynamic_table_.GetEntryCount();
    if (static_cast<uint64_t>(abs) < evicted) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: entry already evicted. abs:%lld", abs);
        return false;
    }
    // DuplicateEntry now correctly accepts absolute index
    if (!dynamic_table_.DuplicateEntry(static_cast<uint32_t>(abs))) {
        LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: duplicate entry failed. abs:%lld", abs);
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

bool QpackEncoder::ReadHeaderPrefix(
    const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, int64_t& base) {
    // RFC 9204 Section 4.5.1: Decode Required Insert Count and Delta Base

    // Decode Encoded Required Insert Count (8-bit prefix)
    uint8_t first = 0;
    uint64_t encoded_ric = 0;
    if (!QpackDecodePrefixedInteger(buffer, QpackHeaderPrefix::kRequiredInsertCountPrefix, first, encoded_ric)) {
        LOG_ERROR("QpackEncoder::ReadHeaderPrefix: decode required insert count failed. encoded_ric:%llu", encoded_ric);
        return false;
    }

    // RFC 9204 Section 4.5.1.1: Decode Required Insert Count
    // If EncodedRIC == 0, RIC == 0
    // Otherwise: MaxEntries = floor(MaxTableCapacity / 32)
    //   FullRange = 2 * MaxEntries
    //   if EncodedRIC > FullRange: error
    //   MaxValue = TotalNumberOfInserts + MaxEntries
    //   MaxWrapped = floor(MaxValue / FullRange) * FullRange
    //   RIC = MaxWrapped + EncodedRIC - 1
    //   if RIC > MaxValue: RIC -= FullRange
    //   if RIC == 0: error
    if (encoded_ric == 0) {
        required_insert_count = 0;
    } else {
        uint64_t max_entries = max_table_capacity_ / 32;
        if (max_entries == 0) {
            max_entries = 1;
        }
        uint64_t full_range = 2 * max_entries;
        if (encoded_ric > full_range) {
            LOG_ERROR(
                "QpackEncoder::ReadHeaderPrefix: encoded RIC exceeds full range. "
                "encoded_ric:%llu, full_range:%llu",
                encoded_ric, full_range);
            return false;
        }
        uint64_t total_inserts = dynamic_table_.GetInsertCount();
        uint64_t max_value = total_inserts + max_entries;
        uint64_t max_wrapped = (max_value / full_range) * full_range;
        required_insert_count = max_wrapped + encoded_ric - 1;
        if (required_insert_count > max_value) {
            if (required_insert_count <= full_range) {
                LOG_ERROR("QpackEncoder::ReadHeaderPrefix: RIC decode underflow");
                return false;
            }
            required_insert_count -= full_range;
        }
        if (required_insert_count == 0) {
            LOG_ERROR("QpackEncoder::ReadHeaderPrefix: decoded RIC is zero but encoded was non-zero");
            return false;
        }
    }

    // Decode Delta Base (7-bit prefix with S bit)
    uint64_t abs_delta_base = 0;
    if (!QpackDecodePrefixedInteger(buffer, QpackHeaderPrefix::kDeltaBasePrefix, first, abs_delta_base)) {
        LOG_ERROR("QpackEncoder::ReadHeaderPrefix: decode delta base failed. abs_delta_base:%llu", abs_delta_base);
        return false;
    }
    bool s_bit = (first & QpackHeaderPrefix::kDeltaBaseSignBit) != 0;

    // Calculate Base from Required Insert Count and Delta Base, RFC 9204 4.5.1:
    //   S = 0 : Base = ReqInsertCount + DeltaBase
    //   S = 1 : Base = ReqInsertCount - DeltaBase - 1
    if (s_bit) {
        base = static_cast<int64_t>(required_insert_count) - static_cast<int64_t>(abs_delta_base) - 1;
        if (base < 0) {
            LOG_ERROR("QpackEncoder::ReadHeaderPrefix: negative base. ric:%llu, delta_base:%llu", required_insert_count,
                abs_delta_base);
            return false;
        }

    } else {
        base = static_cast<int64_t>(required_insert_count) + static_cast<int64_t>(abs_delta_base);
    }

    return true;
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
bool QpackEncoder::DecodeString(const std::shared_ptr<common::IBuffer> buffer, std::string& output) {
    // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
    uint8_t first_byte = 0;
    uint64_t length = 0;
    if (!QpackDecodePrefixedInteger(buffer, QpackString::kLengthPrefix, first_byte, length)) {
        LOG_ERROR("QpackEncoder::DecodeString: decode prefixed integer failed. length:%llu", length);
        return false;
    }

    bool huffman = (first_byte & QpackString::kHuffmanBit) != 0;

    if (length == 0) {
        output.clear();
        return true;
    }

    // Read encoded string
    if (huffman) {
        std::vector<uint8_t> encoded;
        encoded.resize(static_cast<size_t>(length));
        if (buffer->Read(encoded.data(), static_cast<uint32_t>(length)) != static_cast<uint32_t>(length)) {
            LOG_ERROR("QpackEncoder::DecodeString: read encoded string failed. length:%llu", length);
            return false;
        }
        output = HuffmanEncoder::Instance().Decode(encoded);
    } else {
        output.resize(static_cast<size_t>(length));
        if (buffer->Read((uint8_t*)output.data(), static_cast<uint32_t>(length)) != static_cast<uint32_t>(length)) {
            LOG_ERROR("QpackEncoder::DecodeString: read encoded string failed. length:%llu", length);
            return false;
        }
    }
    return true;
}

}  // namespace http3
}  // namespace quicx
