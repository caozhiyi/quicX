#ifndef UTEST_CONNECTION_MOCK_QUIC_CONNECTION
#define UTEST_CONNECTION_MOCK_QUIC_CONNECTION

#include <vector>
#include <cstdint>
#include <cstring>
#include "quic/include/if_quic_stream.h"
#include "quic/include/if_quic_connection.h"

namespace quicx {
namespace quic {

class MockQuicConnection:
    public IQuicConnection {
public:
    MockQuicConnection() {}

    void SetPeer(std::shared_ptr<MockQuicConnection> peer) { peer_ = peer; }

    virtual void SetUserData(void* user_data) { user_data_ = user_data; }
    virtual void* GetUserData() { return user_data_; }

    virtual void GetLocalAddr(std::string& addr, uint32_t& port) {}
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) {}

    // close the connection gracefully. that means all the streams will be closed gracefully.
    virtual void Close();

    // close the connection immediately. that means all the streams will be closed immediately.
    virtual void Reset(uint32_t error_code);

    // create a new stream, only supported send stream and bidirection stream.
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type);

    // set the callback function to handle the stream state change.
    virtual void SetStreamStateCallBack(stream_state_callback cb);

private:
    void* user_data_;
    stream_state_callback stream_state_cb_;

    std::weak_ptr<MockQuicConnection> peer_;
};

}
}

#endif  // MOCK_QUIC_RECV_STREAM_H 