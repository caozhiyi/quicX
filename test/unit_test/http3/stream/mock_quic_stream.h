#ifndef UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM
#define UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM


#include <cstdint>
#include <memory>
#include <quicx/quic/if_quic_bidirection_stream.h>
#include <quicx/quic/if_quic_recv_stream.h>
#include <quicx/quic/if_quic_send_stream.h>
#include <utility>
#include <vector>

namespace quicx {
namespace common {
class IBuffer;
}
namespace quic {

class MockQuicStream:
    public virtual IQuicBidirectionStream,
    public virtual IQuicSendStream,
    public virtual IQuicRecvStream {
public:
    MockQuicStream():
        stream_id_(0),
        direction_(StreamDirection::kBidi) {}
    explicit MockQuicStream(uint64_t stream_id):
        stream_id_(stream_id),
        direction_(StreamDirection::kBidi) {}
    MockQuicStream(uint64_t stream_id, StreamDirection dir):
        stream_id_(stream_id),
        direction_(dir) {}

    void SetPeer(std::shared_ptr<MockQuicStream> peer) { peer_ = peer; }

    // Test helper: assign a unique stream id. Production code keys
    // IConnection::streams_ by stream id, so duplicate ids cause silent
    // collisions where one stream object overwrites another in the map.
    void SetStreamID(uint64_t id) { stream_id_ = id; }

    // Test helper: override the reported direction. The HTTP/3 layer uses
    // GetDirection() to dispatch incoming streams (e.g. ClientConnection
    // refuses bidirectional streams from server with H3_FRAME_UNEXPECTED),
    // so the mock must report the correct direction for each stream that
    // the production code path opens — kSend / kRecv on the *local* side,
    // kRecv / kSend on the peer side, kBidi on both sides.
    void SetDirection(StreamDirection dir) { direction_ = dir; }

    virtual StreamDirection GetDirection() override;
    virtual uint64_t GetStreamID() override;

    virtual void Close() override;

    virtual void Reset(uint32_t error) override;

    virtual void SetStreamReadCallBack(stream_read_callback cb) override;

    virtual int32_t Send(uint8_t* data, uint32_t len) override;
    virtual int32_t Send(std::shared_ptr<IBufferRead> buffer) override;

    virtual std::shared_ptr<IBufferWrite> GetSendBuffer() override;
    virtual bool Flush() override;

    virtual void SetStreamWriteCallBack(stream_write_callback cb) override;

    virtual uint64_t GetPendingSendBytes() override;

    // Test-only helper: directly invoke the registered read callback to
    // simulate the QUIC layer delivering a STREAM frame with a specific
    // is_last (FIN) flag. The production code paths (peer->Send /
    // peer->Flush) always pass is_last=false; FIN is modelled by Close(),
    // which delivers an empty buffer with is_last=true. Tests that need
    // out-of-band FIN patterns (e.g. HEADERS without FIN followed by an
    // empty STREAM with FIN) can still call this directly.
    void SimulateRead(std::shared_ptr<IBufferRead> buf, bool is_last, uint32_t error = 0) {
        if (read_cb_) {
            read_cb_(buf, is_last, error);
        }
    }

    // Test-only helpers used by the matching cpp file when forwarding data
    // between paired streams. Exposed publicly so the helper free function
    // in mock_quic_tream.cpp can buffer or deliver pending bytes without
    // friending the peer object.
    const stream_read_callback& ReadCb() const { return read_cb_; }
    void QueuePendingInbound(std::shared_ptr<IBufferRead> buf, bool is_last) {
        pending_inbound_data_.emplace_back(std::move(buf), is_last);
    }

private:
    void* user_data_;
    stream_read_callback read_cb_;
    stream_write_callback write_cb_;

    std::weak_ptr<MockQuicStream> peer_;
    std::shared_ptr<common::IBuffer> send_buffer_;
    uint64_t stream_id_;
    StreamDirection direction_;

    // FIN delivery is one-shot: a second Close() on the same send direction
    // must not deliver a second FIN (production code may legitimately call
    // Close() more than once during stream teardown).
    bool fin_sent_ = false;

    // Inbound bytes that arrived before SetStreamReadCallBack() installed
    // a read callback. The HTTP/3 Init() flow opens the QPACK encoder /
    // decoder unidirectional streams *before* the receiving side has
    // adopted them and wired up a reader, so without buffering, the
    // stream-type byte and any preamble would be silently dropped.
    std::vector<std::pair<std::shared_ptr<IBufferRead>, bool>> pending_inbound_data_;
};

}  // namespace quic
}  // namespace quicx

#endif  // UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM