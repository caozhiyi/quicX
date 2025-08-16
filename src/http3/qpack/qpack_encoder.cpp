#include <cstdint>
#include "http3/qpack/static_table.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace http3 {

bool QpackEncoder::Encode(const std::unordered_map<std::string, std::string>& headers, 
    std::shared_ptr<common::IBufferWrite> buffer) {
    
    if (!buffer) {
        return false;
    }

    // Write header block prefix per RFC 9204 (prefixed integers)
    uint64_t ric = dynamic_table_.GetEntryCount();
    int64_t base = static_cast<int64_t>(ric);
    WriteHeaderPrefix(buffer, ric, base);

    // Encode each header field
    for (const auto& header : headers) {
        const std::string& name = header.first;
        const std::string& value = header.second;

        // Try to find in static table first
        int32_t index = StaticTable::Instance().FindHeaderItemIndex(name, value);
        if (index >= 0) {
            // Indexed Header Field — static table (1 1 index)
            QpackEncodePrefixedInteger(buffer, 6, 0xC0, static_cast<uint64_t>(index));
            continue;
        }

        // Name-only match in static table
        index = StaticTable::Instance().FindHeaderItemIndex(name);
        if (index >= 0) {
            // Literal Header Field With Name Reference — static (01 1 index)
            QpackEncodePrefixedInteger(buffer, 6, 0x40 | 0x20, static_cast<uint64_t>(index));
            EncodeString(value, buffer);
            continue;
        }

        // No match in static table. Try dynamic table if enabled
        if (enable_dynamic_table_) {
            int32_t dindex = dynamic_table_.FindHeaderItemIndex(name, value);
            if (dindex >= 0) {
                // Indexed Header Field — dynamic (1 0 relative_index)
                // relative_index = base - 1 - absolute_index; base == ric here
                uint64_t relative = static_cast<uint64_t>(base - 1 - dindex);
                QpackEncodePrefixedInteger(buffer, 6, 0x80, relative);
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

            // Literal Header Field With Literal Name — no indexing
            uint8_t literal = 0x20; // 001 pattern (no indexing)
            buffer->Write(&literal, 1);
            EncodeString(name, buffer);
            EncodeString(value, buffer);
            continue;
        }

        // Fallback: encode as literal header field with literal name
        uint8_t literal = 0x20; // Set pattern to 001
        buffer->Write(&literal, 1);
        EncodeString(name, buffer);
        EncodeString(value, buffer);
    }   

    return true;
}

bool QpackEncoder::Decode(const std::shared_ptr<common::IBufferRead> buffer,
    std::unordered_map<std::string, std::string>& headers) {
    
    if (!buffer || buffer->GetDataLength() < 2) {
        return false;
    }

    // Read Required Insert Count and Base
    uint64_t required_insert_count = 0;
    int64_t base = 0;
    if (!ReadHeaderPrefix(buffer, required_insert_count, base)) {
        return false;
    }
    // If required_insert_count > current inserted count, this header block is blocked
    if (required_insert_count > dynamic_table_.GetEntryCount()) {
        // Signal blocked; in full RFC flow, should queue this header block and return
        return false;
    }

    while (buffer->GetDataLength() > 0) {
        uint8_t first_byte;
        buffer->Read(&first_byte, 1);

        auto decode_after_first = [&](uint8_t first, uint8_t prefix_bits, uint64_t& value)->bool {
            if (prefix_bits == 0 || prefix_bits > 8) return false;
            uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
            value = static_cast<uint64_t>(first & max_in_prefix);
            if (value < max_in_prefix) return true;
            uint64_t m = 0;
            uint8_t b = 0;
            do {
                if (buffer->Read(&b, 1) != 1) return false;
                value += static_cast<uint64_t>(b & 0x7fu) << m;
                m += 7;
            } while (b & 0x80u);
            return true;
        };

        if ((first_byte & 0xC0) == 0xC0) {
            // Indexed — static
            uint64_t sidx = 0;
            if (!decode_after_first(first_byte, 6, sidx)) return false;
            auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
            if (!item) return false;
            headers[item->name_] = item->value_;
        } else if ((first_byte & 0xC0) == 0x80) {
            // Indexed — dynamic; value is relative index (6-bit prefix)
            uint64_t rel = 0;
            if (!decode_after_first(first_byte, 6, rel)) return false;
            int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
            if (abs_index < 0) return false;
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs_index));
            if (!item) return false;
            headers[item->name_] = item->value_;
        } else if ((first_byte & 0xE0) == 0x60) {
            // Literal with name reference — static (01 1 index)
            uint64_t sidx = 0;
            if (!decode_after_first(first_byte, 5, sidx)) return false;
            auto item = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(sidx));
            if (!item) return false;
            std::string value;
            if (!DecodeString(buffer, value)) return false;
            headers[item->name_] = value;
        } else if ((first_byte & 0xE0) == 0x40) {
            // Literal with name reference — dynamic (01 0 rel)
            uint64_t rel = 0;
            if (!decode_after_first(first_byte, 5, rel)) return false;
            int64_t abs_index = base - 1 - static_cast<int64_t>(rel);
            if (abs_index < 0) return false;
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs_index));
            if (!item) return false;
            std::string value;
            if (!DecodeString(buffer, value)) return false;
            headers[item->name_] = value;
        } else if ((first_byte & 0xE0) == 0x20) {
            // Literal without indexing — literal name and value
            std::string name, value;
            if (!DecodeString(buffer, name) || !DecodeString(buffer, value)) return false;
            headers[name] = value;
        } else {
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
    if (!instr_buf) return false;
    if (set_capacity) {
        // Set Dynamic Table Capacity: 001xxxxx with 5-bit prefix
        if (!QpackEncodePrefixedInteger(instr_buf, 5, 0x20, new_capacity)) return false;
    }
    if (duplicate_index >= 0) {
        // Duplicate: 0001xxxx with 4-bit prefix
        if (!QpackEncodePrefixedInteger(instr_buf, 4, 0x10, static_cast<uint64_t>(duplicate_index))) return false;
    }
    for (const auto& p : inserts) {
        if (with_name_ref) {
            // Insert With Name Reference:
            // 1 S NPNNNNN (prefix=6 for index); S=1 static, S=0 dynamic; NPNNNNN carries index with 6-bit prefix
            int32_t s_name_idx = StaticTable::Instance().FindHeaderItemIndex(p.first);
            int32_t d_name_idx = dynamic_table_.FindHeaderNameIndex(p.first);
            bool is_static = s_name_idx >= 0;
            uint8_t mask = 0x80; // MSB=1
            if (is_static) {
                // S=1, encode static index directly with 6-bit prefix under 01 1? Actually for encoder stream: 1 S NameIdx
                mask |= 0x40; // set S-bit
                if (!QpackEncodePrefixedInteger(instr_buf, 6, mask, static_cast<uint64_t>(s_name_idx))) return false;
            } else {
                // S=0, dynamic name index relative to current Insert Count (absolute index to relative per RFC 9204)
                uint64_t ric = dynamic_table_.GetEntryCount();
                // If name not found, fall back to Insert Without Name Reference
                if (d_name_idx < 0) {
                    // 01NNNNNN (Insert Without Name Reference) with 6-bit name length prefix
                    if (!QpackEncodePrefixedInteger(instr_buf, 6, 0x40, 0)) return false;
                    if (!QpackEncodeStringLiteral(p.first, instr_buf, false)) return false;
                    if (!QpackEncodeStringLiteral(p.second, instr_buf, false)) return false;
                    continue;
                }
                // dynamic relative index = ric - 1 - absolute_index
                uint64_t relative = static_cast<uint64_t>(static_cast<int64_t>(ric) - 1 - static_cast<int64_t>(d_name_idx));
                if (!QpackEncodePrefixedInteger(instr_buf, 6, mask, relative)) return false;
            }
            if (!QpackEncodeStringLiteral(p.second, instr_buf, false)) return false;
        } else {
            // Insert Without Name Reference: 01NNNNNN name literal then value literal
            if (!QpackEncodePrefixedInteger(instr_buf, 6, 0x40, 0)) return false;
            if (!QpackEncodeStringLiteral(p.first, instr_buf, false)) return false;
            if (!QpackEncodeStringLiteral(p.second, instr_buf, false)) return false;
        }
    }
    return true;
}

bool QpackEncoder::DecodeEncoderInstructions(const std::shared_ptr<common::IBufferRead> instr_buf) {
    if (!instr_buf) return false;
    while (instr_buf->GetDataLength() > 0) {
        uint8_t fb = 0;
        auto decode_after_first = [&](uint8_t first, uint8_t prefix_bits, uint64_t& value)->bool {
            if (prefix_bits == 0 || prefix_bits > 8) return false;
            uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
            value = static_cast<uint64_t>(first & max_in_prefix);
            if (value < max_in_prefix) return true;
            uint64_t m = 0;
            uint8_t b = 0;
            do {
                if (instr_buf->Read(&b, 1) != 1) return false;
                value += static_cast<uint64_t>(b & 0x7fu) << m;
                m += 7;
            } while (b & 0x80u);
            return true;
        };
        if (instr_buf->Read(&fb, 1) != 1) return false;
        if (fb & 0x80) {
            // Insert With Name Reference: 1 S NPNNNNN, index is a prefixed integer with 6-bit prefix
            uint64_t idx = 0;
            if (!decode_after_first(fb, 6, idx)) return false;
            bool is_static = (fb & 0x40) != 0; // S bit
            std::string value;
            if (!QpackDecodeStringLiteral(instr_buf, value)) return false;
            std::string name;
            if (is_static) {
                auto hi = StaticTable::Instance().FindHeaderItem(static_cast<uint32_t>(idx));
                if (!hi) return false;
                name = hi->name_;
            } else {
                // dynamic: idx is relative; convert to absolute = ric - 1 - idx
                uint64_t ric = dynamic_table_.GetEntryCount();
                int64_t abs = static_cast<int64_t>(ric) - 1 - static_cast<int64_t>(idx);
                if (abs < 0) return false;
                auto hi = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs));
                if (!hi) return false;
                name = hi->name_;
            }
            dynamic_table_.AddHeaderItem(name, value);
        } else if ((fb & 0xC0) == 0x40) {
            // Insert Without Name Reference: 01NNNNNN, then name literal + value literal
            uint64_t ignore = 0;
            if (!decode_after_first(fb, 6, ignore)) return false;
            std::string name, value;
            if (!QpackDecodeStringLiteral(instr_buf, name)) return false;
            if (!QpackDecodeStringLiteral(instr_buf, value)) return false;
            dynamic_table_.AddHeaderItem(name, value);
        } else if ((fb & 0xE0) == 0x20) {
            // Set Dynamic Table Capacity: 001xxxxx with 5-bit prefix
            uint64_t cap = 0;
            if (!decode_after_first(fb, 5, cap)) return false;
            dynamic_table_.UpdateMaxTableSize(static_cast<uint32_t>(cap));
        } else if ((fb & 0xF0) == 0x10) {
            // Duplicate: 0001xxxx with 4-bit prefix
            uint64_t rel = 0;
            if (!decode_after_first(fb, 4, rel)) return false;
            uint64_t ric = dynamic_table_.GetEntryCount();
            int64_t abs = static_cast<int64_t>(ric) - 1 - static_cast<int64_t>(rel);
            if (abs < 0) return false;
            auto item = dynamic_table_.FindHeaderItem(static_cast<uint32_t>(abs));
            if (item) dynamic_table_.AddHeaderItem(item->name_, item->value_);
        } else {
            break;
        }
    }
    return true;
}

void QpackEncoder::WriteHeaderPrefix(std::shared_ptr<common::IBufferWrite> buffer, uint64_t required_insert_count, int64_t base) {
    QpackEncodePrefixedInteger(buffer, 8, 0x00, required_insert_count);
    uint64_t ric = dynamic_table_.GetEntryCount();
    int64_t delta = base - static_cast<int64_t>(ric);
    bool s_bit = delta < 0;
    uint64_t abs_delta = static_cast<uint64_t>(s_bit ? -delta : delta);
    uint8_t mask = s_bit ? 0x80 : 0x00;
    QpackEncodePrefixedInteger(buffer, 7, mask, abs_delta);
}

bool QpackEncoder::ReadHeaderPrefix(const std::shared_ptr<common::IBufferRead> buffer, uint64_t& required_insert_count, int64_t& base) {
    uint8_t first = 0;
    if (!QpackDecodePrefixedInteger(buffer, 8, first, required_insert_count)) return false;
    uint64_t ric = required_insert_count;
    uint64_t abs_delta = 0;
    if (!QpackDecodePrefixedInteger(buffer, 7, first, abs_delta)) return false;
    bool s_bit = (first & 0x80) != 0;
    int64_t delta = s_bit ? -static_cast<int64_t>(abs_delta) : static_cast<int64_t>(abs_delta);
    base = static_cast<int64_t>(ric) + delta;
    return true;
}

void QpackEncoder::WritePrefix(std::shared_ptr<common::IBufferWrite> buffer, uint64_t required_insert_count, uint64_t base) {
    // Simple 2-byte demo: lower byte = RIC (cap 255), next byte = BASE (cap 255)
    uint8_t ric8 = static_cast<uint8_t>(required_insert_count & 0xFF);
    uint8_t base8 = static_cast<uint8_t>(base & 0xFF);
    buffer->Write(&ric8, 1);
    buffer->Write(&base8, 1);
}

bool QpackEncoder::ReadPrefix(const std::shared_ptr<common::IBufferRead> buffer, uint64_t& required_insert_count, uint64_t& base) {
    if (buffer->GetDataLength() < 2) return false;
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
        std::vector<uint8_t> encoded;
        encoded = HuffmanEncoder::Instance().Encode(str);
        
        // Write length prefix with H bit set
        uint8_t len = encoded.size() | 0x80;
        buffer->Write(&len, 1);
        
        // Write Huffman-encoded string
        buffer->Write(encoded.data(), encoded.size());
    } else {
        // Write length prefix without H bit
        uint8_t len = str.length();
        buffer->Write(&len, 1);
        
        // Write string directly
        buffer->Write((uint8_t*)str.data(), str.length());
    }
}

// Helper function to decode a string that may be Huffman encoded
bool QpackEncoder::DecodeString(const std::shared_ptr<common::IBufferRead> buffer, std::string& output) {
    // Read length byte and huffman flag
    uint8_t len_byte;
    buffer->Read(&len_byte, 1);
    bool huffman = len_byte & 0x80;
    uint32_t length = len_byte & 0x7F;

    // Read encoded string
    if (huffman) {
        std::vector<uint8_t> encoded;
        encoded.resize(length);
        buffer->Read(encoded.data(), length);
        output = HuffmanEncoder::Instance().Decode(encoded);

    } else {
        output.resize(length);
        buffer->Read((uint8_t*)output.data(), length);
    }
    return true;
}

}
}
