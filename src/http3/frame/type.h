#ifndef HTTP3_FRAME_TYPE
#define HTTP3_FRAME_TYPE

#include <cstdint>

namespace quicx {
namespace http3 {

enum FrameType : uint16_t {
    kData = 0x00,
    kHeaders = 0x01,
    kCancelPush = 0x03,
    kSettings = 0x04,
    kPushPromise = 0x05,
    kGoAway = 0x07,
    kMaxPushId = 0x0d,

    kUnknown = 0xff,
};

// returned by frame Decode() methods
enum class DecodeResult {
    kSuccess,       // Frame decoded successfully
    kNeedMoreData,  // Insufficient data, need to wait for more (not an error)
    kError          // Decode error (malformed frame, invalid data, etc.)
};

// QPACK encoder stream (instructions from encoder to decoder)
constexpr uint8_t kQpackSetCapacityPrefixBits = 5;        // 001xxxxx
constexpr uint8_t kQpackSetCapacityFirstByteMask = 0x20;  // 0010 0000

constexpr uint8_t kQpackInsertWithNameRefPrefixBits = 6;        // 1 S i i i i i i
constexpr uint8_t kQpackInsertWithNameRefFirstByteBase = 0x80;  // 1000 0000
constexpr uint8_t kQpackInsertWithNameRefStaticBit = 0x40;      // 0100 0000 (S bit)

constexpr uint8_t kQpackInsertWithoutNameRefPrefixBits = 6;        // 01 n n n n n n
constexpr uint8_t kQpackInsertWithoutNameRefFirstByteBase = 0x40;  // 0100 0000

constexpr uint8_t kQpackDuplicatePrefixBits = 4;        // 0001 xxxx
constexpr uint8_t kQpackDuplicateFirstByteBase = 0x10;  // 0001 0000

constexpr uint8_t kQpackTop2BitsMask = 0xC0;  // 1100 0000

// QPACK decoder stream (instructions from decoder to encoder)
constexpr uint8_t kQpackDecoderVarintPrefixBits = 8;        // full byte prefix for simple varints
constexpr uint8_t kQpackDecoderVarintFirstByteMask = 0x00;  // no high-bit flags in first byte

// Decoder instruction first-byte patterns (RFC 9204 §4.4)
constexpr uint8_t kQpackDecSectionAckPrefixBits = 7;        // 1xxxxxxx
constexpr uint8_t kQpackDecSectionAckFirstByteMask = 0x80;  // 1000 0000

constexpr uint8_t kQpackDecStreamCancelPrefixBits = 6;        // 01xxxxxx
constexpr uint8_t kQpackDecStreamCancelFirstByteMask = 0x40;  // 0100 0000

constexpr uint8_t kQpackDecInsertCountIncPrefixBits = 6;        // 00xxxxxx
constexpr uint8_t kQpackDecInsertCountIncFirstByteMask = 0x00;  // 0000 0000

}  // namespace http3
}  // namespace quicx

#endif