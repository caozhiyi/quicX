#include "common/buffer/buffer.h"
#include "http3/stream/qpack_decoder_sender_stream.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

QpackDecoderSenderStream::QpackDecoderSenderStream(const std::shared_ptr<quic::IQuicSendStream>& stream,
    const std::function<void(uint64_t stream_id, uint32_t error_code)>& error_handler)
    : IStream(error_handler), stream_(stream) {}

QpackDecoderSenderStream::~QpackDecoderSenderStream() {
    if (stream_) stream_->Close();
}

bool QpackDecoderSenderStream::EnsureStreamPreamble() {
    if (wrote_type_) return true;
    // QPACK decoder stream type 0x03
    uint8_t pre[1] = {0x03};
    auto buf = std::make_shared<common::Buffer>(pre, sizeof(pre));
    buf->MoveWritePt(sizeof(pre));
    if (stream_->Send(buf) <= 0) return false;
    wrote_type_ = true;
    return true;
}

bool QpackDecoderSenderStream::SendTypeAndVarint(uint8_t type, uint64_t value) {
    if (!EnsureStreamPreamble()) return false;
    uint8_t t = type;
    auto buf = std::make_shared<common::Buffer>(&t, 1);
    buf->MoveWritePt(1);
    if (stream_->Send(buf) <= 0) return false;
    uint8_t tmp[16] = {0};
    auto vbuf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
    QpackEncodePrefixedInteger(vbuf, 8, 0x00, value);
    return stream_->Send(vbuf) > 0;
}

bool QpackDecoderSenderStream::SendSectionAck(uint64_t header_block_id) {
    if (!EnsureStreamPreamble()) return false;
    // header_block_id is composed as (stream_id << 32) | section_number
    uint64_t stream_id = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id >> 32));
    uint64_t section_number = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id & 0xffffffffULL));
    uint8_t t = 0x00;
    auto tbuf = std::make_shared<common::Buffer>(&t, 1);
    tbuf->MoveWritePt(1);
    if (stream_->Send(tbuf) <= 0) return false;
    uint8_t tmp1[16] = {0};
    auto vbuf1 = std::make_shared<common::Buffer>(tmp1, sizeof(tmp1));
    if (!QpackEncodePrefixedInteger(vbuf1, 8, 0x00, stream_id)) return false;
    if (stream_->Send(vbuf1) <= 0) return false;
    uint8_t tmp2[16] = {0};
    auto vbuf2 = std::make_shared<common::Buffer>(tmp2, sizeof(tmp2));
    if (!QpackEncodePrefixedInteger(vbuf2, 8, 0x00, section_number)) return false;
    return stream_->Send(vbuf2) > 0;
}

bool QpackDecoderSenderStream::SendStreamCancel(uint64_t header_block_id) {
    if (!EnsureStreamPreamble()) return false;
    uint64_t stream_id = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id >> 32));
    uint64_t section_number = static_cast<uint64_t>(static_cast<uint32_t>(header_block_id & 0xffffffffULL));
    uint8_t t = 0x01;
    auto tbuf = std::make_shared<common::Buffer>(&t, 1);
    tbuf->MoveWritePt(1);
    if (stream_->Send(tbuf) <= 0) return false;
    uint8_t tmp1[16] = {0};
    auto vbuf1 = std::make_shared<common::Buffer>(tmp1, sizeof(tmp1));
    if (!QpackEncodePrefixedInteger(vbuf1, 8, 0x00, stream_id)) return false;
    if (stream_->Send(vbuf1) <= 0) return false;
    uint8_t tmp2[16] = {0};
    auto vbuf2 = std::make_shared<common::Buffer>(tmp2, sizeof(tmp2));
    if (!QpackEncodePrefixedInteger(vbuf2, 8, 0x00, section_number)) return false;
    return stream_->Send(vbuf2) > 0;
}

bool QpackDecoderSenderStream::SendInsertCountIncrement(uint64_t delta) {
    return SendTypeAndVarint(0x02, delta);
}

}
}


