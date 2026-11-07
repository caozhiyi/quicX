#include "common/log/log.h"

#include "http3/qpack/qpack_constants.h"
#include "http3/qpack/qpack_decoder.h"
#include "http3/qpack/static_table.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

bool QpackDecoder::Decode(
    const std::shared_ptr<common::IBuffer> buffer, std::vector<std::pair<std::string, std::string>>& headers) {
    last_decode_status_ = DecodeStatus::kMalformed;  // terminal default until proven otherwise
    if (!buffer || buffer->GetDataLength() < 2) {
        LOG_ERROR("QpackDecoder::Decode: buffer is null or data length is less than 2");
        return false;
    }

    // Read Required Insert Count and Base
    uint64_t required_insert_count = 0;
    int64_t base = 0;
    if (!ReadHeaderPrefix(buffer, required_insert_count, base)) {
        LOG_ERROR("QpackDecoder::Decode: read header prefix failed");
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
            "QpackDecoder::Decode: required insert count is greater than current inserted "
            "count.required_insert_count:%llu, current_inserted_count:%llu",
            required_insert_count, dynamic_table_.GetInsertCount());
        last_decode_status_ = DecodeStatus::kBlocked;
        return false;
    }

    // RFC 9114 §4.2.2: enforce the field-section size DURING the decode, per
    // emitted field, so a small reference cannot expand before the check runs.
    uint64_t section_size_so_far = 0;
    size_t next_unchecked_field = headers.size();

    // Dispatch loop: each wire pattern (RFC 9204 §4.5) decodes into its own
    // method; this loop only consumes the pattern byte and routes.
    // P1-H5e: |required_insert_count| is passed to every dynamic-table
    // reference decoder: RFC 9204 §2.1.2 makes RIC the exclusive upper
    // bound for every referenced absolute index ("insert count of the last
    // referenced entry, plus one"), so a reference at abs >= RIC is a
    // malformed block even when the entry happens to still sit in the
    // table — accepting it would let a block decode successfully now and
    // then fail (or worse, resolve differently) after that entry is
    // evicted.
    while (buffer->GetDataLength() > 0) {
        uint8_t first_byte;
        buffer->Read(&first_byte, 1);

        bool ok = false;
        if ((first_byte & QpackHeaderPattern::kIndexedStaticMask) == QpackHeaderPattern::kIndexedStatic) {
            ok = DecodeIndexedStatic(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kIndexedDynamicMask) == QpackHeaderPattern::kIndexedDynamic) {
            ok = DecodeIndexedDynamic(buffer, first_byte, base, required_insert_count, headers);
        } else if ((first_byte & QpackHeaderPattern::kLiteralNameRefStaticMask) ==
                   QpackHeaderPattern::kLiteralNameRefStatic) {
            ok = DecodeLiteralNameRefStatic(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kLiteralNameRefDynamicMask) ==
                   QpackHeaderPattern::kLiteralNameRefDynamic) {
            ok = DecodeLiteralNameRefDynamic(buffer, first_byte, base, required_insert_count, headers);
        } else if ((first_byte & QpackHeaderPattern::kLiteralNoNameRefMask) == QpackHeaderPattern::kLiteralNoNameRef) {
            ok = DecodeLiteralNoNameRef(buffer, first_byte, base, headers);
        } else if ((first_byte & QpackHeaderPattern::kPostBaseIndexedMask) == QpackHeaderPattern::kPostBaseIndexed) {
            ok = DecodePostBaseIndexed(buffer, first_byte, base, required_insert_count, headers);
        } else if ((first_byte & QpackHeaderPattern::kPostBaseLiteralNameRefMask) ==
                   QpackHeaderPattern::kPostBaseLiteralNameRef) {
            ok = DecodePostBaseLiteralNameRef(buffer, first_byte, base, required_insert_count, headers);
        } else {
            LOG_ERROR("QpackDecoder::Decode: unknown header pattern. first_byte:%d", first_byte);
            return false;
        }
        if (!ok) {
            return false;
        }

        // P0-S6: account for every field this pattern just emitted.
        if (field_section_size_limit_ > 0) {
            while (next_unchecked_field < headers.size()) {
                const auto& f = headers[next_unchecked_field++];
                // RFC 9114 §4.2.2: per-field overhead keeps many tiny fields
                // from evading the limit.
                section_size_so_far += f.first.size() + f.second.size() + 32;
            }
            if (section_size_so_far > field_section_size_limit_) {
                LOG_ERROR(
                    "QpackDecoder::Decode: decompressed field section %llu exceeds limit %llu — "
                    "probable decompression bomb (stream aborted before further expansion)",
                    (unsigned long long)section_size_so_far, (unsigned long long)field_section_size_limit_);
                last_decode_status_ = DecodeStatus::kSectionTooLarge;
                return false;
            }
        }
    }

    last_decode_status_ = DecodeStatus::kOk;
    return true;
}

bool QpackDecoder::DecodeIndexedStatic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t /*base*/, std::vector<std::pair<std::string, std::string>>& headers) {
    // Indexed — static (11xxxxxx)
    uint64_t sidx = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kIndexedStaticPrefix, first_byte, sidx)) {
        LOG_ERROR("QpackDecoder::Decode: decode indexed static failed. sidx:%llu", sidx);
        return false;
    }
    auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
    if (!item) {
        LOG_ERROR("QpackDecoder::Decode: find header item failed. sidx:%llu", sidx);
        return false;
    }
    headers.emplace_back(item->name_, item->value_);
    return true;
}

bool QpackDecoder::DecodeIndexedDynamic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
    uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers) {
    // Indexed — dynamic (10xxxxxx)
    uint64_t rel = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kIndexedDynamicPrefix, first_byte, rel)) {
        LOG_ERROR("QpackDecoder::Decode: decode indexed dynamic failed. rel:%llu", rel);
        return false;
    }
    int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
    // P1-H5e (RFC 9204 §2.1.2): every dynamic reference must stay below the
    // field section's Required Insert Count.
    if (abs_index < 0 || static_cast<uint64_t>(abs_index) >= required_insert_count) {
        LOG_ERROR("QpackDecoder::Decode: absolute index out of RIC range. abs_index:%lld, ric:%llu", abs_index,
            (unsigned long long)required_insert_count);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackDecoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    headers.emplace_back(item->name_, item->value_);
    return true;
}

bool QpackDecoder::DecodeLiteralNameRefStatic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t /*base*/, std::vector<std::pair<std::string, std::string>>& headers) {
    // Literal with name reference — static (0101xxxx, T=1)
    uint64_t sidx = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kLiteralNameRefStaticPrefix, first_byte, sidx)) {
        LOG_ERROR("QpackDecoder::Decode: decode literal name ref static failed. sidx:%llu", sidx);
        return false;
    }
    auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
    if (!item) {
        LOG_ERROR("QpackDecoder::Decode: find header item failed. sidx:%llu", sidx);
        return false;
    }
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackDecoder::Decode: decode string failed. name:%s, value:%s", item->name_.c_str(),
            value.c_str());
        return false;
    }
    headers.emplace_back(item->name_, value);
    return true;
}

bool QpackDecoder::DecodeLiteralNameRefDynamic(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t base, uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers) {
    // Literal with name reference — dynamic (010xxxxx)
    uint64_t rel = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kLiteralNameRefDynamicPrefix, first_byte, rel)) {
        LOG_ERROR("QpackDecoder::Decode: decode literal name ref dynamic failed. rel:%llu", rel);
        return false;
    }
    int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
    // P1-H5e (RFC 9204 §2.1.2): every dynamic reference must stay below the
    // field section's Required Insert Count.
    if (abs_index < 0 || static_cast<uint64_t>(abs_index) >= required_insert_count) {
        LOG_ERROR("QpackDecoder::Decode: absolute index out of RIC range. abs_index:%lld, ric:%llu", abs_index,
            (unsigned long long)required_insert_count);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackDecoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackDecoder::Decode: decode string failed. value:%s", value.c_str());
        return false;
    }
    headers.emplace_back(item->name_, value);
    return true;
}

bool QpackDecoder::DecodeLiteralNoNameRef(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t /*base*/, std::vector<std::pair<std::string, std::string>>& headers) {
    // RFC 9204 Section 4.5.6: Literal Field Line With Literal Name (001xxxxx)
    // Format: 001 N H NameLen(3+) | Name | H ValueLen(7+) | Value
    // first_byte already consumed, contains N, H bits and 3-bit name length prefix
    bool name_huffman = (first_byte & 0x08) != 0;  // H bit for name (bit 3)
    uint64_t name_len = 0;
    // Name length uses 3-bit prefix (bits 0-2)
    if (!QpackDecodePrefixedIntegerFrom(buffer, 3, first_byte, name_len)) {
        LOG_ERROR("QpackDecoder::Decode: decode name length failed for LiteralNoNameRef");
        return false;
    }

    // Read name string.
    //
    // |name_len| comes straight off the wire (a varint, up to 2^62-1) and this
    // is the same unbounded-allocation pattern DecodeString() used to have:
    // resize first, read second. Route through QpackReadStringBody() so the
    // length is bounded by the bytes actually present before anything is
    // allocated.
    std::string name;
    if (!QpackReadStringBody(buffer, name_len, name_huffman, name)) {
        LOG_ERROR("QpackDecoder::Decode: read name string failed. len:%llu, readable:%u", name_len,
            buffer ? buffer->GetDataLength() : 0u);
        return false;
    }

    // Read value using DecodeString (7-bit prefix with H bit)
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackDecoder::Decode: decode value string failed. name:%s", name.c_str());
        return false;
    }

    LOG_DEBUG("QpackDecoder::Decode: LiteralNoNameRef decoded header: %s=%s, remaining=%u", name.c_str(),
        value.c_str(), buffer->GetDataLength());
    headers.emplace_back(name, value);
    return true;
}

bool QpackDecoder::DecodePostBaseIndexed(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte, int64_t base,
    uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers) {
    // RFC 9204 Section 4.5.3: Post-Base Indexed Header Field (0001xxxx)
    // Index is relative to Base, references entries inserted AFTER Base
    uint64_t post_base_index = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kPostBaseIndexedPrefix, first_byte,
            post_base_index)) {
        LOG_ERROR("QpackDecoder::Decode: decode post base indexed failed. post_base_index:%llu", post_base_index);
        return false;
    }
    // Convert to absolute index: abs_index = Base + post_base_index
    int64_t abs_index = base + static_cast<int64_t>(post_base_index);
    // Upper bound is the monotonic insert count, not the post-eviction
    // deque size: a valid absolute index lives in [evicted_count,
    // insert_count); FindHeaderItemByAbsoluteIndex below catches the
    // lower bound (returns nullptr on already-evicted entries).
    // P1-H5e (RFC 9204 §2.1.2): the field section's RIC is additionally
    // the exclusive upper bound for every referenced index.
    if (abs_index < 0 || static_cast<uint64_t>(abs_index) >= required_insert_count ||
        abs_index >= static_cast<int64_t>(dynamic_table_.GetInsertCount())) {
        LOG_ERROR("QpackDecoder::Decode: absolute index is out of range. abs_index:%lld, ric:%llu", abs_index,
            (unsigned long long)required_insert_count);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackDecoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    headers.emplace_back(item->name_, item->value_);
    return true;
}

bool QpackDecoder::DecodePostBaseLiteralNameRef(const std::shared_ptr<common::IBuffer>& buffer, uint8_t first_byte,
    int64_t base, uint64_t required_insert_count, std::vector<std::pair<std::string, std::string>>& headers) {
    // RFC 9204 Section 4.5.5: Literal Header Field With Post-Base Name Reference (0000xxxx)
    // Name reference is relative to Base, value is literal
    uint64_t post_base_index = 0;
    if (!QpackDecodePrefixedIntegerFrom(buffer, QpackHeaderPattern::kPostBaseLiteralNameRefPrefix, first_byte,
            post_base_index)) {
        LOG_ERROR("QpackDecoder::Decode: decode post base literal name ref failed. post_base_index:%llu",
            post_base_index);
        return false;
    }
    // Convert to absolute index: abs_index = Base + post_base_index
    int64_t abs_index = base + static_cast<int64_t>(post_base_index);
    // Same bound choice as DecodePostBaseIndexed (see note there).
    // P1-H5e (RFC 9204 §2.1.2): the field section's RIC is additionally
    // the exclusive upper bound for every referenced index.
    if (abs_index < 0 || static_cast<uint64_t>(abs_index) >= required_insert_count ||
        abs_index >= static_cast<int64_t>(dynamic_table_.GetInsertCount())) {
        LOG_ERROR("QpackDecoder::Decode: absolute index is out of range. abs_index:%lld, ric:%llu", abs_index,
            (unsigned long long)required_insert_count);
        return false;
    }
    auto item = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs_index));
    if (!item) {
        LOG_ERROR("QpackDecoder::Decode: find header item failed. abs_index:%lld", abs_index);
        return false;
    }
    std::string value;
    if (!DecodeString(buffer, value)) {
        LOG_ERROR("QpackDecoder::Decode: decode string failed. value:%s", value.c_str());
        return false;
    }
    headers.emplace_back(item->name_, value);
    return true;
}

bool QpackDecoder::DecodeEncoderInstructions(const std::shared_ptr<common::IBuffer> instr_buf) {
    if (!instr_buf) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: instr_buf is null");
        return false;
    }

    // Dispatch loop: each instruction pattern (RFC 9204 §4.3) decodes into
    // its own method; the per-instruction body lives in
    // DecodeOneEncoderInstruction so streaming callers can parse with
    // snapshot/commit semantics (P1-H5a).
    while (instr_buf->GetDataLength() > 0) {
        if (!DecodeOneEncoderInstruction(instr_buf)) {
            return false;
        }
    }
    return true;
}

bool QpackDecoder::DecodeOneEncoderInstruction(const std::shared_ptr<common::IBuffer> instr_buf) {
    uint8_t fb = 0;
    if (instr_buf->Read(&fb, 1) != 1) {
        LOG_ERROR("QpackDecoder::DecodeOneEncoderInstruction: read byte failed. fb:%d", fb);
        return false;
    }

    if (fb & QpackEncoderInstr::kInsertWithNameRefMask) {
        return DecodeInstrInsertWithNameRef(instr_buf, fb);
    } else if ((fb & QpackEncoderInstr::kInsertWithoutNameRefMask) == QpackEncoderInstr::kInsertWithoutNameRef) {
        return DecodeInstrInsertWithLiteralName(instr_buf, fb);
    } else if ((fb & QpackEncoderInstr::kSetDynamicTableCapacityMask) == QpackEncoderInstr::kSetDynamicTableCapacity) {
        return DecodeInstrSetCapacity(instr_buf, fb);
    } else if ((fb & QpackEncoderInstr::kDuplicateMask) == QpackEncoderInstr::kDuplicate) {
        return DecodeInstrDuplicate(instr_buf, fb);
    }
    LOG_ERROR("QpackDecoder::DecodeOneEncoderInstruction: unknown instruction. fb:%d", fb);
    return false;
}

bool QpackDecoder::DecodeInstrInsertWithNameRef(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // Insert With Name Reference (1Sxxxxxx)
    uint64_t idx = 0;
    if (!QpackDecodePrefixedIntegerFrom(instr_buf, QpackEncoderInstr::kInsertWithNameRefPrefix, fb, idx)) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: decode insert with name ref failed. idx:%llu", idx);
        return false;
    }
    bool is_static = (fb & QpackEncoderInstr::kInsertWithNameRefStaticBit) != 0;  // S bit
    std::string value;
    if (!QpackDecodeStringLiteral(instr_buf, value)) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: decode string literal failed. value:%s", value.c_str());
        return false;
    }
    std::string name;
    if (is_static) {
        auto hi = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(idx));
        if (!hi) {
            LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: find header item failed. idx:%llu", idx);
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
            LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: absolute index is less than 0. abs:%lld", abs);
            return false;
        }
        auto hi = dynamic_table_.FindHeaderItemByAbsoluteIndex(static_cast<uint64_t>(abs));
        if (!hi) {
            LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: find header item failed. abs:%lld", abs);
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
            "QpackDecoder::DecodeEncoderInstructions: AddHeaderItem rejected oversized entry "
            "(name_len=%zu value_len=%zu max_size=%u)",
            name.size(), value.size(), max_table_capacity_);
        return false;
    }
    return true;
}

bool QpackDecoder::DecodeInstrInsertWithLiteralName(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // Insert With Literal Name (01Hxxxxx), RFC 9204 Section 4.3.3.
    // The name length lives in this very byte, so it must be decoded
    // from |fb| rather than from a fresh 8-bit prefix string literal.
    std::string name, value;
    if (!QpackDecodeStringLiteralWithPrefix(instr_buf, fb, QpackEncoderInstr::kInsertWithoutNameRefPrefix,
            QpackEncoderInstr::kInsertWithoutNameRefHuffmanBit, name)) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: decode string literal failed. name:%s", name.c_str());
        return false;
    }
    if (!QpackDecodeStringLiteral(instr_buf, value)) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: decode string literal failed. value:%s", value.c_str());
        return false;
    }
    // See note in DecodeInstrInsertWithNameRef: oversized inserts must be a
    // protocol error.
    if (!dynamic_table_.AddHeaderItem(name, value)) {
        LOG_ERROR(
            "QpackDecoder::DecodeEncoderInstructions: AddHeaderItem rejected oversized entry "
            "(name_len=%zu value_len=%zu max_size=%u)",
            name.size(), value.size(), max_table_capacity_);
        return false;
    }
    return true;
}

bool QpackDecoder::DecodeInstrSetCapacity(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // RFC 9204 Section 4.3.1: Set Dynamic Table Capacity (001xxxxx)
    uint64_t cap = 0;
    if (!QpackDecodePrefixedIntegerFrom(instr_buf, QpackEncoderInstr::kSetDynamicTableCapacityPrefix, fb, cap)) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: decode set dynamic table capacity failed. cap:%llu", cap);
        return false;
    }

    // RFC 9204 Section 3.2.3: Validate capacity against SETTINGS_QPACK_MAX_TABLE_CAPACITY
    if (cap > max_table_capacity_) {
        // Decoder MUST treat this as a connection error (QPACK_ENCODER_STREAM_ERROR)
        LOG_ERROR(
            "QpackDecoder::DecodeEncoderInstructions: capacity is greater than max table capacity. cap:%llu, "
            "max_table_capacity:%u",
            cap, max_table_capacity_);
        return false;
    }

    dynamic_table_.UpdateMaxTableSize(static_cast<uint32_t>(cap));
    return true;
}

bool QpackDecoder::DecodeInstrDuplicate(const std::shared_ptr<common::IBuffer>& instr_buf, uint8_t fb) {
    // RFC 9204 Section 4.3.4: Duplicate instruction (000xxxxx)
    uint64_t rel = 0;
    if (!QpackDecodePrefixedIntegerFrom(instr_buf, QpackEncoderInstr::kDuplicatePrefix, fb, rel)) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: decode duplicate failed. rel:%llu", rel);
        return false;
    }
    uint64_t insert_count = dynamic_table_.GetInsertCount();
    int64_t abs = static_cast<int64_t>(insert_count) - 1 - static_cast<int64_t>(rel);
    if (abs < 0) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: absolute index is less than 0. abs:%lld", abs);
        return false;
    }
    // Check if entry has been evicted
    uint64_t evicted = dynamic_table_.GetInsertCount() - dynamic_table_.GetEntryCount();
    if (static_cast<uint64_t>(abs) < evicted) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: entry already evicted. abs:%lld", abs);
        return false;
    }
    // DuplicateEntry now correctly accepts absolute index
    if (!dynamic_table_.DuplicateEntry(static_cast<uint32_t>(abs))) {
        LOG_ERROR("QpackDecoder::DecodeEncoderInstructions: duplicate entry failed. abs:%lld", abs);
        return false;
    }
    return true;
}

bool QpackDecoder::ReadHeaderPrefix(
    const std::shared_ptr<common::IBuffer> buffer, uint64_t& required_insert_count, int64_t& base) {
    // RFC 9204 Section 4.5.1: Decode Required Insert Count and Delta Base

    // Decode Encoded Required Insert Count (8-bit prefix)
    uint8_t first = 0;
    uint64_t encoded_ric = 0;
    if (!QpackDecodePrefixedInteger(buffer, QpackHeaderPrefix::kRequiredInsertCountPrefix, first, encoded_ric)) {
        LOG_ERROR("QpackDecoder::ReadHeaderPrefix: decode required insert count failed. encoded_ric:%llu", encoded_ric);
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
                "QpackDecoder::ReadHeaderPrefix: encoded RIC exceeds full range. "
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
                LOG_ERROR("QpackDecoder::ReadHeaderPrefix: RIC decode underflow");
                return false;
            }
            required_insert_count -= full_range;
        }
        if (required_insert_count == 0) {
            LOG_ERROR("QpackDecoder::ReadHeaderPrefix: decoded RIC is zero but encoded was non-zero");
            return false;
        }
    }

    // Decode Delta Base (7-bit prefix with S bit)
    uint64_t abs_delta_base = 0;
    if (!QpackDecodePrefixedInteger(buffer, QpackHeaderPrefix::kDeltaBasePrefix, first, abs_delta_base)) {
        LOG_ERROR("QpackDecoder::ReadHeaderPrefix: decode delta base failed. abs_delta_base:%llu", abs_delta_base);
        return false;
    }
    bool s_bit = (first & QpackHeaderPrefix::kDeltaBaseSignBit) != 0;

    // Calculate Base from Required Insert Count and Delta Base, RFC 9204 4.5.1:
    //   S = 0 : Base = ReqInsertCount + DeltaBase
    //   S = 1 : Base = ReqInsertCount - DeltaBase - 1
    if (s_bit) {
        base = static_cast<int64_t>(required_insert_count) - static_cast<int64_t>(abs_delta_base) - 1;
        if (base < 0) {
            LOG_ERROR("QpackDecoder::ReadHeaderPrefix: negative base. ric:%llu, delta_base:%llu", required_insert_count,
                abs_delta_base);
            return false;
        }

    } else {
        base = static_cast<int64_t>(required_insert_count) + static_cast<int64_t>(abs_delta_base);
    }

    return true;
}

bool QpackDecoder::DecodeString(const std::shared_ptr<common::IBuffer> buffer, std::string& output) {
    // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
    uint8_t first_byte = 0;
    uint64_t length = 0;
    if (!QpackDecodePrefixedInteger(buffer, QpackString::kLengthPrefix, first_byte, length)) {
        LOG_ERROR("QpackDecoder::DecodeString: decode prefixed integer failed. length:%llu", length);
        return false;
    }

    bool huffman = (first_byte & QpackString::kHuffmanBit) != 0;

    // |length| is whatever the peer wrote (a varint, up to 2^62-1). It MUST be
    // bounded by the bytes actually readable before allocating: the old code
    // resized to |length| first and only then tried to read, so a single
    // HEADERS frame could request gigabytes and the resulting std::bad_alloc /
    // std::length_error propagated out of the HTTP/3 stream callback into the
    // event loop, terminating the process.
    //
    // QpackReadStringBody() performs exactly that bound (and is the same helper
    // the instruction-stream decoders use), so route through it rather than
    // keeping a second, unguarded copy here.
    if (!QpackReadStringBody(buffer, length, huffman, output)) {
        LOG_ERROR("QpackDecoder::DecodeString: read string body failed. length:%llu, readable:%u", length,
            buffer ? buffer->GetDataLength() : 0u);
        return false;
    }
    return true;
}
}  // namespace http3
}  // namespace quicx
