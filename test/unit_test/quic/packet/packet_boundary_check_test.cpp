#include <gtest/gtest.h>

#include "quic/packet/init_packet.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/decode/decode.h"

namespace quicx {
namespace quic {
namespace {

static const uint16_t kBufLen = 128;

// Helper: create a buffer with raw bytes
std::shared_ptr<common::SingleBlockBuffer> MakeBuffer(const uint8_t* data, size_t len) {
    auto buffer = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(kBufLen));
    buffer->Write(data, len);
    return buffer;
}

// Test InitPacket: token_length exceeds buffer boundary
TEST(packet_boundary_utest, init_packet_token_length_overflow) {
    // Build a minimal init packet after header:
    // token_length(varint) = 0xFF (255, far exceeds remaining buffer)
    // We need to first encode a valid header, then append malicious payload
    
    uint8_t raw[64] = {0};
    uint8_t pos = 0;

    // Long header flag: form=1, fixed=1, type=Initial(00), PN len = 0
    raw[pos++] = 0xC0;
    // Version
    raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x01;
    // DCID len=4, DCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x01; raw[pos++] = 0x02; raw[pos++] = 0x03; raw[pos++] = 0x04;
    // SCID len=4, SCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x05; raw[pos++] = 0x06; raw[pos++] = 0x07; raw[pos++] = 0x08;

    // Token length varint: encode 255 (far exceeds remaining)
    uint8_t* token_len_start = raw + pos;
    uint8_t* token_len_end = common::EncodeVarint(token_len_start, raw + sizeof(raw), 255);
    pos += (token_len_end - token_len_start);

    // Only a few bytes remain, not 255
    auto buffer = MakeBuffer(raw, pos + 5);

    InitPacket pkt(raw[0]);
    // Skip the flag byte for decode
    buffer->MoveReadPt(1); // skip flag we already parsed
    EXPECT_FALSE(pkt.DecodeWithoutCrypto(buffer, false));
}

// Test InitPacket: length field exceeds buffer boundary
TEST(packet_boundary_utest, init_packet_length_overflow) {
    uint8_t raw[64] = {0};
    uint8_t pos = 0;

    // Long header flag
    raw[pos++] = 0xC0;
    // Version
    raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x01;
    // DCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x01; raw[pos++] = 0x02; raw[pos++] = 0x03; raw[pos++] = 0x04;
    // SCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x05; raw[pos++] = 0x06; raw[pos++] = 0x07; raw[pos++] = 0x08;

    // Token length = 0 (varint)
    uint8_t* vp = common::EncodeVarint(raw + pos, raw + sizeof(raw), 0);
    pos += (vp - (raw + pos));

    // Length = 1000 (far exceeds remaining buffer)
    vp = common::EncodeVarint(raw + pos, raw + sizeof(raw), 1000);
    pos += (vp - (raw + pos));

    auto buffer = MakeBuffer(raw, pos + 5);

    InitPacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    EXPECT_FALSE(pkt.DecodeWithoutCrypto(buffer, false));
}

// Test HandshakePacket: length field exceeds buffer boundary
TEST(packet_boundary_utest, handshake_packet_length_overflow) {
    uint8_t raw[64] = {0};
    uint8_t pos = 0;

    // Long header flag: handshake type = 0b10
    raw[pos++] = 0xE0; // form=1, fixed=1, type=Handshake(10)
    // Version
    raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x01;
    // DCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x01; raw[pos++] = 0x02; raw[pos++] = 0x03; raw[pos++] = 0x04;
    // SCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x05; raw[pos++] = 0x06; raw[pos++] = 0x07; raw[pos++] = 0x08;

    // Length = 5000 (exceeds remaining)
    uint8_t* vp = common::EncodeVarint(raw + pos, raw + sizeof(raw), 5000);
    pos += (vp - (raw + pos));

    auto buffer = MakeBuffer(raw, pos + 5);

    HandshakePacket pkt(raw[0]);
    buffer->MoveReadPt(1);
    EXPECT_FALSE(pkt.DecodeWithoutCrypto(buffer, false));
}

// Test Rtt0Packet: length field exceeds buffer boundary
TEST(packet_boundary_utest, rtt0_packet_length_overflow) {
    uint8_t raw[64] = {0};
    uint8_t pos = 0;

    // Long header flag: 0-RTT type = 0b01
    raw[pos++] = 0xD0; // form=1, fixed=1, type=0-RTT(01)
    // Version
    raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x00; raw[pos++] = 0x01;
    // DCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x01; raw[pos++] = 0x02; raw[pos++] = 0x03; raw[pos++] = 0x04;
    // SCID
    raw[pos++] = 0x04;
    raw[pos++] = 0x05; raw[pos++] = 0x06; raw[pos++] = 0x07; raw[pos++] = 0x08;

    // Length = 3000 (exceeds remaining)
    uint8_t* vp = common::EncodeVarint(raw + pos, raw + sizeof(raw), 3000);
    pos += (vp - (raw + pos));

    auto buffer = MakeBuffer(raw, pos + 5);

    Rtt0Packet pkt(raw[0]);
    buffer->MoveReadPt(1);
    EXPECT_FALSE(pkt.DecodeWithoutCrypto(buffer, false));
}

}  // namespace
}  // namespace quic
}  // namespace quicx
