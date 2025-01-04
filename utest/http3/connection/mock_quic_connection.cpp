#include "utest/http3/stream/mock_quic_stream.h"
#include "utest/http3/connection/mock_quic_connection.h"

namespace quicx {
namespace quic {

void MockQuicConnection::Close() {
    // TODO: implement this
}

void MockQuicConnection::Reset(uint32_t error_code) {
    // TODO: implement this
}

std::shared_ptr<IQuicStream> MockQuicConnection::MakeStream(StreamDirection type) {
    auto stream1 = std::make_shared<MockQuicStream>();
    auto stream2 = std::make_shared<MockQuicStream>();
    stream1->SetPeer(stream2);
    stream2->SetPeer(stream1);

    if (type == StreamDirection::SD_SEND) {
        auto peer = peer_.lock();
        if (peer || peer->stream_state_cb_) {
            peer->stream_state_cb_(stream2, StreamDirection::SD_RECV);
        }
        

    } else {
        auto peer = peer_.lock();
        if (peer || peer->stream_state_cb_) {
            peer->stream_state_cb_(stream2, StreamDirection::SD_BIDI);
        }
    }
    return stream1;
}

void MockQuicConnection::SetStreamStateCallBack(stream_state_callback cb) {
    stream_state_cb_ = cb;
}

}
}