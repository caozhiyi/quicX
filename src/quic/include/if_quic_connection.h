#ifndef QUIC_INCLUDE_IF_QUIC_CONNECTION
#define QUIC_INCLUDE_IF_QUIC_CONNECTION

#include <string>
#include "quic/include/type.h"

namespace quicx {
namespace quic {

/*
 indicates a quic connection.
*/
class IQuicConnection {
public:
    IQuicConnection() {}
    virtual ~IQuicConnection() {}

    virtual void SetUserData(void* user_data) = 0;
    virtual void* GetUserData() = 0;

    virtual void GetLocalAddr(std::string& addr, uint32_t& port) = 0;
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) = 0;

    // close the connection gracefully. that means all the streams will be closed gracefully.
    virtual void Close() = 0;

    // close the connection immediately. that means all the streams will be closed immediately.
    virtual void Reset(uint32_t error_code) = 0;

    // create a new stream, only supported send stream and bidirection stream.
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) = 0;

    // set the callback function to handle the stream state change.
    virtual void SetStreamStateCallBack(stream_state_callback cb) = 0;
};

}
}

#endif