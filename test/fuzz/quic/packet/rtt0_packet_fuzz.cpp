#include <cstddef>
#include <cstdint>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/packet/rtt_0_packet.h"
#include "test_cryptographer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    {
        // Wrap input as a read buffer
        auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(size));
        in->Write(data, size);

        quicx::quic::Rtt0Packet packet;
        if (!packet.DecodeWithoutCrypto(in, true)) {
            return 0;
        }

        // Encode packet
        auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(2048));
        if (!packet.Encode(out)) {
            return 0;
        }
        // Decode the re-encoded packet again to exercise the decode path
        quicx::quic::Rtt0Packet packet2;
        (void)packet2.DecodeWithoutCrypto(out, true);
    }

    {
        // Wrap input as a read buffer
        auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(size));
        in->Write(data, size);

        quicx::quic::Rtt0Packet packet;
        packet.SetCryptographer(PacketTest::Instance().GetTestClientCryptographer());
        // DecodeWithCrypto starts from packet_src_data_, and only
        // DecodeWithoutCrypto ever assigns it. Calling it on a freshly
        // constructed packet -- as this harness used to -- ran the entire crypto
        // path over an empty span, so header-protection sampling and the payload
        // length arithmetic were never actually fuzzed.
        if (!packet.DecodeWithoutCrypto(in, true)) {
            return 0;
        }
        if (!packet.DecodeWithCrypto(in)) {
            return 0;
        }

        // Encode packet
        auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(2048));
        if (!packet.Encode(out)) {
            return 0;
        }

        // Decode the re-encoded packet again to exercise the decode path
        quicx::quic::Rtt0Packet packet2;
        packet2.SetCryptographer(PacketTest::Instance().GetTestClientCryptographer());
        if (packet2.DecodeWithoutCrypto(out, true)) {
            (void)packet2.DecodeWithCrypto(out);
        }
    }

    return 0;
}