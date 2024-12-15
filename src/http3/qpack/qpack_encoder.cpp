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

    // Write required prefix - currently using 0 for both required insert count and base
    uint8_t prefix[2] = {0, 0};
    buffer->Write(prefix, 2);

    // Encode each header field
    for (const auto& header : headers) {
        const std::string& name = header.first;
        const std::string& value = header.second;

        // Try to find in static table first
        int32_t index = StaticTable::Instance().FindHeaderItemIndex(name, value);
        if (index >= 0) {
            // Found exact match in static table
            // Encode as indexed header field
            uint8_t indexed = 0x80 | (index & 0x7F); // Set first bit to 1
            buffer->Write(&indexed, 1);
            continue;
        }

        // Name-only match in static table
        index = StaticTable::Instance().FindHeaderItemIndex(name);
        if (index >= 0) {
            // Encode as literal header field with name reference
            uint8_t literal = 0x40 | (index & 0x3F); // Set pattern to 01
            buffer->Write(&literal, 1);
            
            // Encode value
            EncodeString(value, buffer);
            continue;
        }

        // No match in static table - encode as literal header field with literal name
        uint8_t literal = 0x20; // Set pattern to 001
        buffer->Write(&literal, 1);

        // Encode name and value
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

    // Read and skip required prefix bytes
    uint8_t prefix[2];
    buffer->Read(prefix, 2);

    while (buffer->GetDataLength() > 0) {
        uint8_t first_byte;
        buffer->Read(&first_byte, 1);

        if (first_byte & 0x80) {
            // Indexed Header Field
            uint32_t index = first_byte & 0x7F;
            auto header_item = StaticTable::Instance().FindHeaderItem(index);
            if (header_item) {
                headers[header_item->name_] = header_item->value_;
            } else {
                return false;
            }

        } else if (first_byte & 0x40) {
            // Literal Header Field With Name Reference
            uint32_t index = first_byte & 0x3F;
            auto header_item = StaticTable::Instance().FindHeaderItem(index);
            if (!header_item) {
                return false;
            }

            // Decode value
            std::string value;
            if (!DecodeString(buffer, value)) {
                return false;
            }

            headers[header_item->name_] = value;
        } else {
            // Literal Header Field With Literal Name
            std::string name, value;

            // Decode name and value
            if (!DecodeString(buffer, name) || !DecodeString(buffer, value)) {
                return false;
            }

            headers[name] = value;
        }
    }

    return true;
}

// Helper function to encode a string with Huffman encoding if beneficial
void QpackEncoder::EncodeString(const std::string& str, std::shared_ptr<common::IBufferWrite> buffer) {
    if (HuffmanEncoder::Instance().ShouldHuffmanEncode(str)) {
        // Encode with Huffman
        std::string encoded;
        encoded = HuffmanEncoder::Instance().Encode(str);
        
        // Write length prefix with H bit set
        uint8_t len = encoded.length() | 0x80;
        buffer->Write(&len, 1);
        
        // Write Huffman-encoded string
        buffer->Write((uint8_t*)encoded.data(), encoded.length());
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
    std::string encoded;
    encoded.resize(length);
    buffer->Read((uint8_t*)encoded.data(), length);

    if (huffman) {
        output = HuffmanEncoder::Instance().Decode(encoded);
    } else {
        output = encoded;
    }
    return true;
}

}
}
