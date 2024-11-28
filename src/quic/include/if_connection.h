#ifndef QUIC_INCLUDE_IF_CONNECTION
#define QUIC_INCLUDE_IF_CONNECTION

#include <string>
#include "quic/include/type.h"

namespace quicx {
namespace quic {

/*
 indicates a quic connection.
*/
class IConnection {
public:
    IConnection() {}
    virtual ~IConnection() {}

    void SetUserData(void* user_data) = 0;
    void* GetUserData() = 0;

    virtual void GetLocalAddr(std::string& addr, uint32_t& port) = 0;
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) = 0;

    // close the connection gracefully. that means all the streams will be closed gracefully.
    virtual void Close() = 0;

    // close the connection immediately. that means all the streams will be closed immediately.
    virtual void Reset() = 0;

    // create a new stream, only supported send stream and bidirection stream.
    virtual std::shared_ptr<ISendStream> MakeStream(StreamType type) = 0;

    // set the callback function to handle the stream state change.
    virtual void SetStreamStateCallBack(stream_state_callback cb) = 0;
};

}
}

#endif