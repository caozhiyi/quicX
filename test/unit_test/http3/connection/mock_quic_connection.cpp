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
    // Hand out a unique stream id per (local connection, peer connection)
    // pair. We use the local side's counter for the local-facing stream
    // and the peer's counter for the peer-facing stream so each side's
    // streams_ map keys are stable; production code never assumes that
    // both endpoints see the same id for the same logical stream
    // (and our HTTP/3 layer here keys purely on local stream id).
    auto peer = peer_.lock();
    uint64_t local_id = next_stream_id_++;
    uint64_t peer_id = peer ? peer->next_stream_id_++ : local_id;

    // Mirror direction onto the peer side: kSend → peer sees kRecv,
    // kRecv → peer sees kSend, kBidi → both bidi. The HTTP/3 stack uses
    // GetDirection() to filter streams (for example,
    // ClientConnection::HandleStream rejects an incoming bidi stream
    // from the server with H3_FRAME_UNEXPECTED) so reporting the wrong
    // direction here cascades into protocol violations on the receiver
    // side and the entire control / QPACK plane never gets wired up.
    StreamDirection peer_direction = type;
    if (type == StreamDirection::kSend) {
        peer_direction = StreamDirection::kRecv;
    } else if (type == StreamDirection::kRecv) {
        peer_direction = StreamDirection::kSend;
    }

    auto stream1 = std::make_shared<MockQuicStream>(local_id, type);
    auto stream2 = std::make_shared<MockQuicStream>(peer_id, peer_direction);
    stream1->SetPeer(stream2);
    stream2->SetPeer(stream1);

    if (peer) {
        if (peer->stream_state_cb_) {
            peer->stream_state_cb_(stream2, 0);
        } else {
            // Peer hasn't installed its stream-state callback yet (typical
            // during the cross-init window when one side runs Init() before
            // the other). Defer delivery until SetStreamStateCallBack() is
            // wired up, mirroring how a real QUIC event loop would defer
            // until the peer is ready to read.
            peer->pending_inbound_streams_.push_back(stream2);
        }
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
    // Drain any inbound streams that arrived before the callback was
    // installed. Without this, every stream that the peer opened during
    // its own Init() (before *we* called Init()) would be silently lost.
    if (stream_state_cb_) {
        std::vector<std::shared_ptr<IQuicStream>> drain;
        drain.swap(pending_inbound_streams_);
        for (auto& s : drain) {
            stream_state_cb_(s, 0);
        }
    }
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