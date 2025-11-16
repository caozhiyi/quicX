#include <cstdint>
#include <cstddef>

#include "test_cryptographer.h"
#include "quic/packet/handshake_packet.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    {
        // Wrap input as a read buffer
        auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(size));
        in->Write(data, size);

        quicx::quic::HandshakePacket packet;
        if (!packet.DecodeWithoutCrypto(in, true)) {
            return 0;
        }

        // Encode packet
        uint8_t out_buf[2048];
        auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(sizeof(out_buf)));
        out->Write(out_buf, sizeof(out_buf));
        if (!packet.Encode(out)) {
            return 0;
        }

        // Decode the re-encoded packet again to exercise the decode path
        auto out_read = out->GetSharedReadableSpan();
        quicx::quic::HandshakePacket packet2;
        (void)packet2.DecodeWithoutCrypto(out, true);
    }

    {
        // Wrap input as a read buffer
        auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(size));
        in->Write(data, size);

        quicx::quic::HandshakePacket packet;
        packet.SetCryptographer(PacketTest::Instance().GetTestClientCryptographer());
        if (!packet.DecodeWithCrypto(in)) {
            return 0;
        }

        // Encode packet
        uint8_t out_buf[2048];
        auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(sizeof(out_buf)));
        out->Write(out_buf, sizeof(out_buf));
        if (!packet.Encode(out)) {
            return 0;
        }

        // Decode the re-encoded packet again to exercise the decode path
        quicx::quic::HandshakePacket packet2;
        (void)packet2.DecodeWithCrypto(out);
    }
    
    return 0;
}