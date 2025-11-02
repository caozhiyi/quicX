#include <cstdint>
#include "common/log/log.h"
#include "http3/qpack/util.h"
#include "http3/qpack/static_table.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/huffman_encoder.h"
#include "http3/qpack/qpack_constants.h"

namespace quicx {
namespace http3 {

bool QpackEncoder::Encode(const std::unordered_map<std::string, std::string>& headers, 
    std::shared_ptr<common::IBufferWrite> buffer) {
    
    if (!buffer) {
        common::LOG_ERROR("QpackEncoder::Encode: buffer is null");
        return false;
    }

    // Write header block prefix per RFC 9204 Section 4.5.1
    // Required Insert Count: minimum number of dynamic table insertions required to decode this block
    // Base: reference point for relative indexing
    
    uint64_t insert_count = dynamic_table_.GetEntryCount();
    
    // Optimization opportunity: We could track which dynamic table entries are actually used
    // in this header block and set Required Insert Count to the minimum needed.
    // Current simple implementation: assume all current entries might be referenced.
    uint64_t required_insert_count = insert_count;
    
    // RFC 9204 Section 4.5.1: Base is the reference point for dynamic table indexing
    // Base = Required Insert Count means all references are "pre-base" (backward references)
    // If we wanted to use Post-Base indexing, we would set Base < Required Insert Count
    // Current implementation: Base = RIC (no Post-Base references in encoding)
    int64_t base = static_cast<int64_t>(required_insert_count);
    
    WriteHeaderPrefix(buffer, required_insert_count, base);

    // Encode each header field
    for (const auto& header : headers) {
        const std::string& name = header.first;
        const std::string& value = header.second;

        // Try to find in static table first
        int32_t index = StaticTable::Instance().FindHeaderItemIndex(name, value);
        if (index >= 0) {
            // Indexed Header Field — static table (11xxxxxx)
            QpackEncodePrefixedInteger(buffer, 
                QpackHeaderPattern::kIndexedStaticPrefix, 
                QpackHeaderPattern::kIndexedStatic, 
                static_cast<uint64_t>(index));
            continue;
        }

        // Name-only match in static table
        index = StaticTable::Instance().FindHeaderItemIndex(name);
        if (index >= 0) {
            // Literal Header Field With Name Reference — static (011xxxxx)
            QpackEncodePrefixedInteger(buffer, 
                QpackHeaderPattern::kLiteralNameRefStaticPrefix, 
                QpackHeaderPattern::kLiteralNameRefStatic, 
                static_cast<uint64_t>(index));
            EncodeString(value, buffer);
            continue;
        }

        // No match in static table. Try dynamic table if enabled
        if (enable_dynamic_table_) {
            int32_t dindex = dynamic_table_.FindHeaderItemIndex(name, value);
            if (dindex >= 0) {
                // Indexed Header Field — dynamic (10xxxxxx)
                // relative_index = base - 1 - absolute_index; base == ric here
                uint64_t relative = static_cast<uint64_t>(base - 1 - dindex);
                QpackEncodePrefixedInteger(buffer, 
                    QpackHeaderPattern::kIndexedDynamicPrefix, 
                    QpackHeaderPattern::kIndexedDynamic, 
                    relative);
                continue;
            }

            // Prefer Insert With Name Reference if name exists in static or dynamic table
            int32_t s_name_idx = StaticTable::Instance().FindHeaderItemIndex(name);
            int32_t d_name_idx = dynamic_table_.FindHeaderNameIndex(name);
            bool with_name_ref = (s_name_idx >= 0) || (d_name_idx >= 0);
            dynamic_table_.AddHeaderItem(name, value);
            if (instruction_sender_) {
                instruction_sender_({{name, value}});
            }

            // Literal Header Field With Literal Name — no indexing (001xxxxx)
            uint8_t literal = QpackHeaderPattern::kLiteralNoNameRef;
            buffer->Write(&literal, 1);
            EncodeString(name, buffer);
            EncodeString(value, buffer);
            continue;
        }

        // Fallback: encode as literal header field with literal name (001xxxxx)
        uint8_t literal = QpackHeaderPattern::kLiteralNoNameRef;
        buffer->Write(&literal, 1);
        EncodeString(name, buffer);
        EncodeString(value, buffer);
    }   

    return true;
}

bool QpackEncoder::Decode(const std::shared_ptr<common::IBufferRead> buffer,
    std::unordered_map<std::string, std::string>& headers) {
    
    if (!buffer || buffer->GetDataLength() < 2) {
        common::LOG_ERROR("QpackEncoder::Decode: buffer is null or data length is less than 2");
        return false;
    }

    // Read Required Insert Count and Base
    uint64_t required_insert_count = 0;
    int64_t base = 0;
    if (!ReadHeaderPrefix(buffer, required_insert_count, base)) {
        common::LOG_ERROR("QpackEncoder::Decode: read header prefix failed");
        return false;
    }
    // If required_insert_count > current inserted count, this header block is blocked
    if (required_insert_count > dynamic_table_.GetEntryCount()) {
        // Signal blocked; in full RFC flow, should queue this header block and return
        common::LOG_ERROR("QpackEncoder::Decode: required insert count is greater than current inserted count.required_insert_count:%llu, current_inserted_count:%llu",
            required_insert_count, dynamic_table_.GetEntryCount());
        return false;
    }

    while (buffer->GetDataLength() > 0) {
        uint8_t first_byte;
        buffer->Read(&first_byte, 1);

        auto decode_after_first = [&](uint8_t first, uint8_t prefix_bits, uint64_t& value)->bool {
            if (prefix_bits == 0 || prefix_bits > 8) {
                common::LOG_ERROR("QpackEncoder::Decode: decode after first failed. prefix_bits:%d", prefix_bits);
                return false;
            }
            
            uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
            value = static_cast<uint64_t>(first & max_in_prefix);
            if (value < max_in_prefix) {
                return true;
            }
            uint64_t m = 0;
            uint8_t b = 0;
            do {
                if (buffer->Read(&b, 1) != 1) {
                    common::LOG_ERROR("QpackEncoder::Decode: read byte failed. b:%d", b);
                    return false;
                }
                value += static_cast<uint64_t>(b & QpackConst::kVarintValueMask) << m;
                m += 7;
            } while (b & QpackConst::kVarintContinueBit);

            return true;
        };

        if ((first_byte & QpackHeaderPattern::kIndexedStaticMask) == QpackHeaderPattern::kIndexedStatic) {
            // Indexed — static (11xxxxxx)
            uint64_t sidx = 0;
            if (!decode_after_first(first_byte, QpackHeaderPattern::kIndexedStaticPrefix, sidx)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode indexed static failed. sidx:%llu", sidx);
                return false;
            }
            auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
            if (!item) {
                common::LOG_ERROR("QpackEncoder::Decode: find header item failed. sidx:%llu", sidx);
                return false;
            }
            headers[item->name_] = item->value_;
            
        } else if ((first_byte & QpackHeaderPattern::kIndexedDynamicMask) == QpackHeaderPattern::kIndexedDynamic) {
            // Indexed — dynamic (10xxxxxx)
            uint64_t rel = 0;
            if (!decode_after_first(first_byte, QpackHeaderPattern::kIndexedDynamicPrefix, rel)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode indexed dynamic failed. rel:%llu", rel);
                return false;
            }
            int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
            if (abs_index < 0) {
                common::LOG_ERROR("QpackEncoder::Decode: absolute index is less than 0. abs_index:%lld", abs_index);
                return false;
            }
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs_index));
            if (!item) {
                common::LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
                return false;
            }
            headers[item->name_] = item->value_;

        } else if ((first_byte & QpackHeaderPattern::kLiteralNameRefStaticMask) == QpackHeaderPattern::kLiteralNameRefStatic) {
            // Literal with name reference — static (011xxxxx)
            uint64_t sidx = 0;
            if (!decode_after_first(first_byte, QpackHeaderPattern::kLiteralNameRefStaticPrefix, sidx)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode literal name ref static failed. sidx:%llu", sidx);
                return false;
            }
            auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
            if (!item) {
                common::LOG_ERROR("QpackEncoder::Decode: find header item failed. sidx:%llu", sidx);
                return false;
            }
            std::string value;
            if (!DecodeString(buffer, value)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode string failed. value:%s", value.c_str());
                return false;
            }
            headers[item->name_] = value;
            
        } else if ((first_byte & QpackHeaderPattern::kLiteralNameRefDynamicMask) == QpackHeaderPattern::kLiteralNameRefDynamic) {
            // Literal with name reference — dynamic (010xxxxx)
            uint64_t rel = 0;
            if (!decode_after_first(first_byte, QpackHeaderPattern::kLiteralNameRefDynamicPrefix, rel)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode literal name ref dynamic failed. rel:%llu", rel);
                return false;
            }
            int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
            if (abs_index < 0) {
                common::LOG_ERROR("QpackEncoder::Decode: absolute index is less than 0. abs_index:%lld", abs_index);
                return false;
            }
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs_index));
            if (!item) {
                common::LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
                return false;
            }
            std::string value;
            if (!DecodeString(buffer, value)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode string failed. value:%s", value.c_str());
                return false;
            }
            headers[item->name_] = value;

        } else if ((first_byte & QpackHeaderPattern::kLiteralNoNameRefMask) == QpackHeaderPattern::kLiteralNoNameRef) {
            // Literal without indexing — literal name and value (001xxxxx)
            std::string name, value;
            if (!DecodeString(buffer, name) || !DecodeString(buffer, value)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode string failed. name:%s, value:%s", name.c_str(), value.c_str());
                return false;
            }
            headers[name] = value;

        } else if ((first_byte & QpackHeaderPattern::kPostBaseIndexedMask) == QpackHeaderPattern::kPostBaseIndexed) {
            // RFC 9204 Section 4.5.3: Post-Base Indexed Header Field (0001xxxx)
            // Index is relative to Base, references entries inserted AFTER Base
            uint64_t post_base_index = 0;
            if (!decode_after_first(first_byte, QpackHeaderPattern::kPostBaseIndexedPrefix, post_base_index)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode post base indexed failed. post_base_index:%llu", post_base_index);
                return false;
            }
            // Convert to absolute index: abs_index = Base + post_base_index
            int64_t abs_index = base + static_cast<int64_t>(post_base_index);
            if (abs_index < 0 || abs_index >= static_cast<int64_t>(dynamic_table_.GetEntryCount())) {
                common::LOG_ERROR("QpackEncoder::Decode: absolute index is out of range. abs_index:%lld", abs_index);
                return false;
            }
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs_index));
            if (!item) {
                common::LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
                return false;
            }
            headers[item->name_] = item->value_;

        } else if ((first_byte & QpackHeaderPattern::kPostBaseLiteralNameRefMask) == QpackHeaderPattern::kPostBaseLiteralNameRef) {
            // RFC 9204 Section 4.5.5: Literal Header Field With Post-Base Name Reference (0000xxxx)
            // Name reference is relative to Base, value is literal
            uint64_t post_base_index = 0;
            if (!decode_after_first(first_byte, QpackHeaderPattern::kPostBaseLiteralNameRefPrefix, post_base_index)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode post base literal name ref failed. post_base_index:%llu", post_base_index);
                return false;
            }
            // Convert to absolute index: abs_index = Base + post_base_index
            int64_t abs_index = base + static_cast<int64_t>(post_base_index);
            if (abs_index < 0 || abs_index >= static_cast<int64_t>(dynamic_table_.GetEntryCount())) {
                common::LOG_ERROR("QpackEncoder::Decode: absolute index is out of range. abs_index:%lld", abs_index);
                return false;
            }
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs_index));
            if (!item) {
                common::LOG_ERROR("QpackEncoder::Decode: find header item failed. abs_index:%lld", abs_index);
                return false;
            }
            std::string value;
            if (!DecodeString(buffer, value)) {
                common::LOG_ERROR("QpackEncoder::Decode: decode string failed. value:%s", value.c_str());
                return false;
            }
            headers[item->name_] = value;

        } else {
            common::LOG_ERROR("QpackEncoder::Decode: unknown header pattern. first_byte:%d", first_byte);
            return false;
        }
    }

    return true;
}

bool QpackEncoder::EncodeEncoderInstructions(const std::vector<std::pair<std::string,std::string>>& inserts,
                                   std::shared_ptr<common::IBufferWrite> instr_buf,
                                   bool with_name_ref,
                                   bool set_capacity,
                                   uint32_t new_capacity,
                                   int32_t duplicate_index) {
    if (!instr_buf) {
        common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: instr_buf is null");
        return false;
    }

    if (set_capacity) {
        // Set Dynamic Table Capacity (001xxxxx)
        if (!QpackEncodePrefixedInteger(instr_buf, 
            QpackEncoderInstr::kSetDynamicTableCapacityPrefix, 
            QpackEncoderInstr::kSetDynamicTableCapacity, 
            new_capacity)) {
            common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode set dynamic table capacity failed. new_capacity:%u", new_capacity);
            return false;
        }
    }

    if (duplicate_index >= 0) {
        // Duplicate (0001xxxx)
        if (!QpackEncodePrefixedInteger(instr_buf, 
            QpackEncoderInstr::kDuplicatePrefix, 
            QpackEncoderInstr::kDuplicate, 
            static_cast<uint64_t>(duplicate_index))) {
            common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode duplicate failed. duplicate_index:%d", duplicate_index);
            return false;
        }
    }

    for (const auto& p : inserts) {
        if (with_name_ref) {
            // Insert With Name Reference (1Sxxxxxx)
            // S=1 static, S=0 dynamic; 6-bit prefix for index
            int32_t s_name_idx = StaticTable::Instance().FindHeaderItemIndex(p.first);
            int32_t d_name_idx = dynamic_table_.FindHeaderNameIndex(p.first);
            bool is_static = s_name_idx >= 0;
            uint8_t mask = QpackEncoderInstr::kInsertWithNameRef;
            if (is_static) {
                // S=1, encode static index
                mask |= QpackEncoderInstr::kInsertWithNameRefStaticBit;
                if (!QpackEncodePrefixedInteger(instr_buf, 
                    QpackEncoderInstr::kInsertWithNameRefPrefix, 
                    mask, 
                    static_cast<uint64_t>(s_name_idx))) {
                    common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode insert with name ref failed. s_name_idx:%d", s_name_idx);
                    return false;
                }

            } else {
                // S=0, dynamic name index relative to current Insert Count (absolute index to relative per RFC 9204)
                uint64_t ric = dynamic_table_.GetEntryCount();
                // If name not found, fall back to Insert Without Name Reference
                if (d_name_idx < 0) {
                    // Insert Without Name Reference (01xxxxxx)
                    if (!QpackEncodePrefixedInteger(instr_buf, 
                        QpackEncoderInstr::kInsertWithoutNameRefPrefix, 
                        QpackEncoderInstr::kInsertWithoutNameRef, 
                        0)) {
                        common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode insert without name ref failed.");
                        return false;
                    }
                    if (!QpackEncodeStringLiteral(p.first, instr_buf, false)) {
                        common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. name:%s", p.first.c_str());
                        return false;
                    }
                    if (!QpackEncodeStringLiteral(p.second, instr_buf, false)) {
                        common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. value:%s", p.second.c_str());
                        return false;
                    }
                    continue;
                }
                // dynamic relative index = ric - 1 - absolute_index
                uint64_t relative = static_cast<uint64_t>(static_cast<int64_t>(ric) - 1 - static_cast<int64_t>(d_name_idx));
                if (!QpackEncodePrefixedInteger(instr_buf, 
                    QpackEncoderInstr::kInsertWithNameRefPrefix, 
                    mask, 
                    relative)) {
                    common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode insert with name ref failed. relative:%llu", relative);
                    return false;
                }
            }

            if (!QpackEncodeStringLiteral(p.second, instr_buf, false)) {
                common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. value:%s", p.second.c_str());
                return false;
            }

        } else {
            // Insert Without Name Reference (01xxxxxx)
            if (!QpackEncodePrefixedInteger(instr_buf, 
                QpackEncoderInstr::kInsertWithoutNameRefPrefix, 
                QpackEncoderInstr::kInsertWithoutNameRef, 
                0)) {
                common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode insert without name ref failed.");
                return false;
            }
            if (!QpackEncodeStringLiteral(p.first, instr_buf, false)) {
                common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. name:%s", p.first.c_str());
                return false;
            }
            if (!QpackEncodeStringLiteral(p.second, instr_buf, false)) {
                common::LOG_ERROR("QpackEncoder::EncodeEncoderInstructions: encode string literal failed. value:%s", p.second.c_str());
                return false;
            }
        }
    }
    return true;
}

bool QpackEncoder::DecodeEncoderInstructions(const std::shared_ptr<common::IBufferRead> instr_buf) {
    if (!instr_buf) {
        common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: instr_buf is null");
        return false;
    }

    while (instr_buf->GetDataLength() > 0) {
        uint8_t fb = 0;
        auto decode_after_first = [&](uint8_t first, uint8_t prefix_bits, uint64_t& value)->bool {
            if (prefix_bits == 0 || prefix_bits > 8) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode after first failed. prefix_bits:%d", prefix_bits);
                return false;
            }
            uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
            value = static_cast<uint64_t>(first & max_in_prefix);
            if (value < max_in_prefix) {
                return true;
            }
            uint64_t m = 0;
            uint8_t b = 0;
            do {
                if (instr_buf->Read(&b, 1) != 1) {
                    common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: read byte failed. b:%d", b);
                    return false;
                }
                value += static_cast<uint64_t>(b & QpackConst::kVarintValueMask) << m;
                m += 7;
            } while (b & QpackConst::kVarintContinueBit);

            return true;
        };
        
        if (instr_buf->Read(&fb, 1) != 1) {
            common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: read byte failed. fb:%d", fb);
            return false;
        }

        if (fb & QpackEncoderInstr::kInsertWithNameRefMask) {
            // Insert With Name Reference (1Sxxxxxx)
            uint64_t idx = 0;
            if (!decode_after_first(fb, QpackEncoderInstr::kInsertWithNameRefPrefix, idx)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode insert with name ref failed. idx:%llu", idx);
                return false;
            }
            bool is_static = (fb & QpackEncoderInstr::kInsertWithNameRefStaticBit) != 0; // S bit
            std::string value;
            if (!QpackDecodeStringLiteral(instr_buf, value)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode string literal failed. value:%s", value.c_str());
                return false;
            }
            std::string name;
            if (is_static) {
                auto hi = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(idx));
                if (!hi) {
                    common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: find header item failed. idx:%llu", idx);
                    return false;
                }
                name = hi->name_;
            } else {
                // dynamic: idx is relative; convert to absolute = ric - 1 - idx
                uint64_t ric = dynamic_table_.GetEntryCount();
                int64_t abs = static_cast<int64_t>(ric) - 1 - static_cast<int64_t>(idx);
                if (abs < 0) {
                    common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: absolute index is less than 0. abs:%lld", abs);
                    return false;
                }
                auto hi = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs));
                if (!hi) {
                    common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: find header item failed. abs:%lld", abs);
                    return false;
                }
                name = hi->name_;
            }
            dynamic_table_.AddHeaderItem(name, value);

        } else if ((fb & QpackEncoderInstr::kInsertWithoutNameRefMask) == QpackEncoderInstr::kInsertWithoutNameRef) {
            // Insert Without Name Reference (01xxxxxx)
            uint64_t ignore = 0;
            if (!decode_after_first(fb, QpackEncoderInstr::kInsertWithoutNameRefPrefix, ignore)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode insert without name ref failed. ignore:%llu", ignore);
                return false;
            }
            std::string name, value;
            if (!QpackDecodeStringLiteral(instr_buf, name)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode string literal failed. name:%s", name.c_str());
                return false;
            }
            if (!QpackDecodeStringLiteral(instr_buf, value)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode string literal failed. value:%s", value.c_str());
                return false;
            }
            dynamic_table_.AddHeaderItem(name, value);

        } else if ((fb & QpackEncoderInstr::kSetDynamicTableCapacityMask) == QpackEncoderInstr::kSetDynamicTableCapacity) {
            // RFC 9204 Section 4.3.1: Set Dynamic Table Capacity (001xxxxx)
            uint64_t cap = 0;
            if (!decode_after_first(fb, QpackEncoderInstr::kSetDynamicTableCapacityPrefix, cap)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode set dynamic table capacity failed. cap:%llu", cap);
                return false;
            }
            
            // RFC 9204 Section 3.2.3: Validate capacity against SETTINGS_QPACK_MAX_TABLE_CAPACITY
            if (cap > max_table_capacity_) {
                // Decoder MUST treat this as a connection error (QPACK_ENCODER_STREAM_ERROR)
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: capacity is greater than max table capacity. cap:%llu, max_table_capacity:%u", cap, max_table_capacity_);
                return false;
            }
            
            dynamic_table_.UpdateMaxTableSize(static_cast<uint32_t>(cap));

        } else if ((fb & QpackEncoderInstr::kDuplicateMask) == QpackEncoderInstr::kDuplicate) {
            // RFC 9204 Section 4.3.4: Duplicate instruction (0001xxxx)
            uint64_t rel = 0;
            if (!decode_after_first(fb, QpackEncoderInstr::kDuplicatePrefix, rel)) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: decode duplicate failed. rel:%llu", rel);
                return false;
            }
            uint64_t ric = dynamic_table_.GetEntryCount();
            int64_t abs = static_cast<int64_t>(ric) - 1 - static_cast<int64_t>(rel);
            if (abs < 0) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: absolute index is less than 0. abs:%lld", abs);
                return false;
            }
            // Use DuplicateEntry method which handles the duplication properly
            if (!dynamic_table_.DuplicateEntry(static_cast<uint32_t>(abs))) {
                common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: duplicate entry failed. abs:%lld", abs);
                return false;
            }

        } else {
            common::LOG_ERROR("QpackEncoder::DecodeEncoderInstructions: unknown instruction. fb:%d", fb);
            break;
        }
    }
    return true;
}

void QpackEncoder::WriteHeaderPrefix(std::shared_ptr<common::IBufferWrite> buffer, uint64_t required_insert_count, int64_t base) {
    // RFC 9204 Section 4.5.1: Encode Required Insert Count and Delta Base
    
    // Encode Required Insert Count (8-bit prefix, 0x00 mask)
    // RFC 9204 Section 4.5.1.1: Encoded Required Insert Count
    uint64_t encoded_ric = 0;
    if (required_insert_count > 0) {
        // For simplicity, we directly encode the value
        // Full RFC algorithm: EncodedRIC = (RIC % (2 * MaxEntries)) + 1
        // Here we assume MaxEntries is large enough that wrapping doesn't occur
        encoded_ric = required_insert_count;
    }
    QpackEncodePrefixedInteger(buffer, 
        QpackHeaderPrefix::kRequiredInsertCountPrefix, 
        0x00, 
        encoded_ric);
    
    // Encode Delta Base (7-bit prefix with S bit)
    // Delta Base = Base - Required Insert Count
    // S=0 if Delta Base >= 0, S=1 if Delta Base < 0
    int64_t delta_base = base - static_cast<int64_t>(required_insert_count);
    bool s_bit = (delta_base < 0);
    uint64_t abs_delta_base = static_cast<uint64_t>(s_bit ? -delta_base : delta_base);
    uint8_t s_mask = s_bit ? QpackHeaderPrefix::kDeltaBaseSignBit : 0x00;
    QpackEncodePrefixedInteger(buffer, 
        QpackHeaderPrefix::kDeltaBasePrefix, 
        s_mask, 
        abs_delta_base);
}

bool QpackEncoder::ReadHeaderPrefix(const std::shared_ptr<common::IBufferRead> buffer, uint64_t& required_insert_count, int64_t& base) {
    // RFC 9204 Section 4.5.1: Decode Required Insert Count and Delta Base
    
    // Decode Encoded Required Insert Count (8-bit prefix)
    uint8_t first = 0;
    uint64_t encoded_ric = 0;
    if (!QpackDecodePrefixedInteger(buffer, 
        QpackHeaderPrefix::kRequiredInsertCountPrefix, 
        first, 
        encoded_ric)) {
        common::LOG_ERROR("QpackEncoder::ReadHeaderPrefix: decode required insert count failed. encoded_ric:%llu", encoded_ric);
        return false;
    }
    
    // For simplicity, we directly use the encoded value
    // Full RFC algorithm would decode: RIC = (EncodedRIC - 1) or handle wrapping
    required_insert_count = encoded_ric;
    
    // Decode Delta Base (7-bit prefix with S bit)
    uint64_t abs_delta_base = 0;
    if (!QpackDecodePrefixedInteger(buffer, 
        QpackHeaderPrefix::kDeltaBasePrefix, 
        first, 
        abs_delta_base)) {
        common::LOG_ERROR("QpackEncoder::ReadHeaderPrefix: decode delta base failed. abs_delta_base:%llu", abs_delta_base);
        return false;
    }
    bool s_bit = (first & QpackHeaderPrefix::kDeltaBaseSignBit) != 0;
    
    // Calculate Base from Required Insert Count and Delta Base
    // Base = Required Insert Count + Delta Base (if S=0)
    // Base = Required Insert Count - Delta Base (if S=1)
    int64_t delta_base = s_bit ? -static_cast<int64_t>(abs_delta_base) : static_cast<int64_t>(abs_delta_base);
    base = static_cast<int64_t>(required_insert_count) + delta_base;
    
    return true;
}

void QpackEncoder::WritePrefix(std::shared_ptr<common::IBufferWrite> buffer, uint64_t required_insert_count, uint64_t base) {
    // Simple 2-byte demo: lower byte = RIC (cap 255), next byte = BASE (cap 255)
    uint8_t ric8 = static_cast<uint8_t>(required_insert_count & QpackConst::kByteMask);
    uint8_t base8 = static_cast<uint8_t>(base & QpackConst::kByteMask);
    buffer->Write(&ric8, 1);
    buffer->Write(&base8, 1);
}

bool QpackEncoder::ReadPrefix(const std::shared_ptr<common::IBufferRead> buffer, uint64_t& required_insert_count, uint64_t& base) {
    if (buffer->GetDataLength() < 2) {
        common::LOG_ERROR("QpackEncoder::ReadPrefix: buffer data length is less than 2");
        return false;
    }
    uint8_t ric8 = 0, base8 = 0;
    buffer->Read(&ric8, 1);
    buffer->Read(&base8, 1);
    required_insert_count = ric8;
    base = base8;
    return true;
}

// Helper function to encode a string with Huffman encoding if beneficial
void QpackEncoder::EncodeString(const std::string& str, std::shared_ptr<common::IBufferWrite> buffer) {
    if (HuffmanEncoder::Instance().ShouldHuffmanEncode(str)) {
        // Encode with Huffman
        std::vector<uint8_t> encoded = HuffmanEncoder::Instance().Encode(str);
        
        // Write length prefix with H bit set (7-bit prefix)
        // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
        QpackEncodePrefixedInteger(buffer, 
            QpackString::kLengthPrefix, 
            QpackString::kHuffmanBit, 
            static_cast<uint64_t>(encoded.size()));
        
        // Write Huffman-encoded string
        buffer->Write(encoded.data(), encoded.size());
    } else {
        // Write length prefix without H bit (7-bit prefix)
        // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
        QpackEncodePrefixedInteger(buffer, 
            QpackString::kLengthPrefix, 
            0x00, 
            static_cast<uint64_t>(str.length()));
        
        // Write string directly
        if (!str.empty()) {
            buffer->Write((uint8_t*)str.data(), str.length());
        }
    }
}

// Helper function to decode a string that may be Huffman encoded
bool QpackEncoder::DecodeString(const std::shared_ptr<common::IBufferRead> buffer, std::string& output) {
    // RFC 9204 Section 4.1.1: String Literal with 7-bit prefixed length
    uint8_t first_byte = 0;
    uint64_t length = 0;
    if (!QpackDecodePrefixedInteger(buffer, 
        QpackString::kLengthPrefix, 
        first_byte, 
        length)) {
        common::LOG_ERROR("QpackEncoder::DecodeString: decode prefixed integer failed. length:%llu", length);
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
        if (buffer->Read(encoded.data(), static_cast<uint32_t>(length)) != static_cast<int32_t>(length)) {
            common::LOG_ERROR("QpackEncoder::DecodeString: read encoded string failed. length:%llu", length);
            return false;
        }
        output = HuffmanEncoder::Instance().Decode(encoded);
    } else {
        output.resize(static_cast<size_t>(length));
        if (buffer->Read((uint8_t*)output.data(), static_cast<uint32_t>(length)) != static_cast<int32_t>(length)) {
            common::LOG_ERROR("QpackEncoder::DecodeString: read encoded string failed. length:%llu", length);
            return false;
        }
    }
    return true;
}

}
}
