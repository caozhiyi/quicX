#ifndef UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM
#define UTEST_HTTP3_STREAM_MOCK_QUIC_STREAM

#include <cstdint>
#include "quic/include/if_quic_stream.h"
#include "quic/include/if_quic_send_stream.h"
#include "quic/include/if_quic_recv_stream.h"
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace quic {

class MockQuicStream:
    public virtual IQuicBidirectionStream,
    public virtual IQuicSendStream,
    public virtual IQuicRecvStream {
public:
    MockQuicStream() {}
    
    void SetPeer(std::shared_ptr<MockQuicStream> peer) { peer_ = peer; }

    virtual quic::StreamDirection GetDirection() override;
    virtual uint64_t GetStreamID() override;

    virtual void Close() override;

    virtual void Reset(uint32_t error) override;

    virtual void SetStreamReadCallBack(quic::stream_read_callback cb);

    virtual int32_t Send(uint8_t* data, uint32_t len);
    virtual int32_t Send(std::shared_ptr<common::IBufferRead> buffer);

    virtual void SetStreamWriteCallBack(stream_write_callback cb);

private:
    void* user_data_;
    stream_read_callback read_cb_;
    stream_write_callback write_cb_;

    std::weak_ptr<MockQuicStream> peer_;
};

}
}

#endif  // MOCK_QUIC_RECV_STREAM_H 