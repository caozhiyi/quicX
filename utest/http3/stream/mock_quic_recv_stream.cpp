
#include "common/buffer/buffer.h"
#include "mock_quic_recv_stream.h"

namespace quicx {
namespace quic {

void MockQuicRecvStream::SetUserData(void* user_data) {
    user_data_ = user_data;
}

void* MockQuicRecvStream::GetUserData() {
    return user_data_;
}

quic::StreamDirection MockQuicRecvStream::GetDirection() {
    return quic::StreamDirection::SD_BIDI;
}

uint64_t MockQuicRecvStream::GetStreamID() {
    return 0;
}

void MockQuicRecvStream::Close() {
    // No-op for mock
}

void MockQuicRecvStream::Reset(uint32_t error) {
    // No-op for mock
}

void MockQuicRecvStream::SetStreamReadCallBack(quic::stream_read_callback cb) {
    read_cb_ = cb;
}

int32_t MockQuicRecvStream::Send(uint8_t* data, uint32_t len) {
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

int32_t MockQuicRecvStream::Send(std::shared_ptr<common::IBufferRead> buffer) {
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

void MockQuicRecvStream::SetStreamWriteCallBack(quic::stream_write_callback cb) {
    write_cb_ = cb;
}

}
}
