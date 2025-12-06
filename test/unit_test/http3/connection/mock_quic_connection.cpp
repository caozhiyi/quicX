#include "test/unit_test/http3/stream/mock_quic_stream.h"
#include "test/unit_test/http3/connection/mock_quic_connection.h"

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
    
    auto peer = peer_.lock();
    if (peer && peer->stream_state_cb_) {
        peer->stream_state_cb_(stream2, 0);
    }
    
    return stream1;
}

bool MockQuicConnection::MakeStreamAsync(StreamDirection type, stream_creation_callback callback) {
    // For mock, immediately create stream and invoke callback
    auto stream = MakeStream(type);
    if (stream && callback) {
        callback(stream);
        return true;
    }
    return false;
}

void MockQuicConnection::SetStreamStateCallBack(stream_state_callback cb) {
    stream_state_cb_ = cb;
}

bool MockQuicConnection::ExportResumptionSession(std::string& out_session_der) {
    // TODO: implement this
    return false;
}

uint64_t MockQuicConnection::AddTimer(timer_callback callback, uint32_t timeout_ms) {
    // For mock, just return a timer ID without actually scheduling
    // In real tests, you might want to actually execute the callback
    (void)callback;
    (void)timeout_ms;
    return next_timer_id_++;
}

void MockQuicConnection::RemoveTimer(uint64_t timer_id) {
    // For mock, do nothing
    (void)timer_id;
}

}
}