
#include "common/buffer/buffer.h"
#include "utest/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace quic {

quic::StreamDirection MockQuicStream::GetDirection() {
    return quic::StreamDirection::SD_BIDI;
}

uint64_t MockQuicStream::GetStreamID() {
    return 0;
}

void MockQuicStream::Close() {
    // No-op for mock
}

void MockQuicStream::Reset(uint32_t error) {
    // No-op for mock
}

void MockQuicStream::SetStreamReadCallBack(quic::stream_read_callback cb) {
    read_cb_ = cb;
}

int32_t MockQuicStream::Send(uint8_t* data, uint32_t len) {
    if (write_cb_) {
        write_cb_(len, 0);
    }

    auto peer = peer_.lock();
    if (peer || peer->read_cb_) {
        std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(data, len);
        peer->read_cb_(buffer, 0);
    }
    return len;
}

int32_t MockQuicStream::Send(std::shared_ptr<common::IBufferRead> buffer) {
    int len = buffer->GetDataLength();
    if (write_cb_) {
        write_cb_(len, 0);
    }

    auto peer = peer_.lock();
    if (peer || peer->read_cb_) {
        peer->read_cb_(buffer, 0);
    }
    return len;
}

void MockQuicStream::SetStreamWriteCallBack(quic::stream_write_callback cb) {
    write_cb_ = cb;
}

}
}
