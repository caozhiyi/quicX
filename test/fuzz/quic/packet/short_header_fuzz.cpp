#include <cstdint>
#include <cstddef>

#include "common/buffer/buffer.h"
#include "quic/packet/header/short_header.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // Wrap input as a read buffer
    auto in = std::make_shared<quicx::common::Buffer>(
        const_cast<uint8_t*>(data), const_cast<uint8_t*>(data) + size);

    quicx::quic::ShortHeader header;
    if (!header.DecodeHeader(in, true)) {
        return 0;
    }

    // Encode decoded header back out
    uint8_t out_buf[2048];
    auto out = std::make_shared<quicx::common::Buffer>(out_buf, out_buf + sizeof(out_buf));
    if (!header.EncodeHeader(out)) {
        return 0;
    }

    // Decode the re-encoded bytes again to exercise the decode path
    auto out_read = out->GetReadViewPtr(0);
    quicx::quic::ShortHeader header2;
    (void)header2.DecodeHeader(out_read, true);

    return 0;
}
