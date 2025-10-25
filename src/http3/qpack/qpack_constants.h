#ifndef HTTP3_QPACK_QPACK_CONSTANTS_H
#define HTTP3_QPACK_QPACK_CONSTANTS_H

#include <cstdint>

namespace quicx {
namespace http3 {

// QPACK Header Block Representation Types (RFC 9204 Section 4.5)
// These are the byte patterns used to identify different header field types
namespace QpackHeaderPattern {
    // Indexed Header Field — Static Table (11xxxxxx)
    constexpr uint8_t kIndexedStatic = 0xC0;
    constexpr uint8_t kIndexedStaticMask = 0xC0;
    constexpr uint8_t kIndexedStaticPrefix = 6;
    
    // Indexed Header Field — Dynamic Table (10xxxxxx)
    constexpr uint8_t kIndexedDynamic = 0x80;
    constexpr uint8_t kIndexedDynamicMask = 0xC0;
    constexpr uint8_t kIndexedDynamicPrefix = 6;
    
    // Literal Header Field With Name Reference — Static (011xxxxx)
    constexpr uint8_t kLiteralNameRefStatic = 0x60;
    constexpr uint8_t kLiteralNameRefStaticMask = 0xE0;
    constexpr uint8_t kLiteralNameRefStaticPrefix = 5;
    
    // Literal Header Field With Name Reference — Dynamic (010xxxxx)
    constexpr uint8_t kLiteralNameRefDynamic = 0x40;
    constexpr uint8_t kLiteralNameRefDynamicMask = 0xE0;
    constexpr uint8_t kLiteralNameRefDynamicPrefix = 5;
    
    // Literal Header Field Without Name Reference (001xxxxx)
    constexpr uint8_t kLiteralNoNameRef = 0x20;
    constexpr uint8_t kLiteralNoNameRefMask = 0xE0;
    constexpr uint8_t kLiteralNoNameRefPrefix = 5;
    
    // Post-Base Indexed Header Field (0001xxxx)
    constexpr uint8_t kPostBaseIndexed = 0x10;
    constexpr uint8_t kPostBaseIndexedMask = 0xF0;
    constexpr uint8_t kPostBaseIndexedPrefix = 4;
    
    // Literal Header Field With Post-Base Name Reference (0000xxxx)
    constexpr uint8_t kPostBaseLiteralNameRef = 0x00;
    constexpr uint8_t kPostBaseLiteralNameRefMask = 0xF0;
    constexpr uint8_t kPostBaseLiteralNameRefPrefix = 4;
}

// QPACK Encoder Stream Instructions (RFC 9204 Section 4.3)
namespace QpackEncoderInstr {
    // Insert With Name Reference (1Sxxxxxx)
    constexpr uint8_t kInsertWithNameRef = 0x80;
    constexpr uint8_t kInsertWithNameRefMask = 0x80;
    constexpr uint8_t kInsertWithNameRefPrefix = 6;
    constexpr uint8_t kInsertWithNameRefStaticBit = 0x40;  // S bit: 1=static, 0=dynamic
    
    // Insert Without Name Reference (01xxxxxx)
    constexpr uint8_t kInsertWithoutNameRef = 0x40;
    constexpr uint8_t kInsertWithoutNameRefMask = 0xC0;
    constexpr uint8_t kInsertWithoutNameRefPrefix = 6;
    
    // Set Dynamic Table Capacity (001xxxxx)
    constexpr uint8_t kSetDynamicTableCapacity = 0x20;
    constexpr uint8_t kSetDynamicTableCapacityMask = 0xE0;
    constexpr uint8_t kSetDynamicTableCapacityPrefix = 5;
    
    // Duplicate (0001xxxx)
    constexpr uint8_t kDuplicate = 0x10;
    constexpr uint8_t kDuplicateMask = 0xF0;
    constexpr uint8_t kDuplicatePrefix = 4;
}

// QPACK String Literal Constants (RFC 9204 Section 4.1.1)
namespace QpackString {
    constexpr uint8_t kHuffmanBit = 0x80;         // H bit: 1=Huffman encoded
    constexpr uint8_t kLengthPrefix = 7;          // String length uses 7-bit prefix
}

// QPACK Header Block Prefix Constants (RFC 9204 Section 4.5.1)
namespace QpackHeaderPrefix {
    constexpr uint8_t kRequiredInsertCountPrefix = 8;  // Required Insert Count uses 8-bit prefix
    constexpr uint8_t kDeltaBasePrefix = 7;            // Delta Base uses 7-bit prefix
    constexpr uint8_t kDeltaBaseSignBit = 0x80;        // S bit: 1=negative, 0=positive
}

// Other QPACK constants
namespace QpackConst {
    constexpr uint8_t kVarintContinueBit = 0x80;  // Varint continuation bit
    constexpr uint8_t kVarintValueMask = 0x7F;     // Varint value bits (lower 7 bits)
    constexpr uint8_t kByteMask = 0xFF;            // Full byte mask
}

}  // namespace http3
}  // namespace quicx

#endif  // HTTP3_QPACK_QPACK_CONSTANTS_H

