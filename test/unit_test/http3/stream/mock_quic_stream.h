#ifndef UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM
#define UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM

#include <cstdint>
#include <memory>
#include <quicx/quic/if_quic_send_stream.h>
#include <quicx/quic/if_quic_recv_stream.h>
#include <quicx/quic/if_quic_bidirection_stream.h>

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
    MockQuicStream() {}
    
    void SetPeer(std::shared_ptr<MockQuicStream> peer) { peer_ = peer; }

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

    // Test-only helper: directly invoke the registered read callback to
    // simulate the QUIC layer delivering a STREAM frame with a specific
    // is_last (FIN) flag. The production code path (peer->Send / peer->Flush)
    // always passes is_last=false because the mock doesn't model FIN. Tests
    // that need to exercise FIN-aware paths (e.g. HEADERS without FIN
    // followed by an empty STREAM with FIN) should call this directly.
    void SimulateRead(std::shared_ptr<IBufferRead> buf, bool is_last, uint32_t error = 0) {
        if (read_cb_) {
            read_cb_(buf, is_last, error);
        }
    }

private:
    void* user_data_;
    stream_read_callback read_cb_;
    stream_write_callback write_cb_;

    std::weak_ptr<MockQuicStream> peer_;
    std::shared_ptr<common::IBuffer> send_buffer_;
};

}
}

#endif  // MOCK_QUIC_RECV_STREAM_H 