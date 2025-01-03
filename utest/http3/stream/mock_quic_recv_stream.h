#ifndef MOCK_QUIC_RECV_STREAM
#define MOCK_QUIC_RECV_STREAM

#include <vector>
#include <cstdint>
#include <cstring>
#include "quic/include/if_quic_bidirection_stream.h"

namespace quicx {
namespace quic {

class MockQuicRecvStream:
    public IQuicBidirectionStream {
public:
    MockQuicRecvStream() {}

    virtual void SetUserData(void* user_data) override;
    virtual void* GetUserData() override;

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
};

}
}

#endif  // MOCK_QUIC_RECV_STREAM_H 