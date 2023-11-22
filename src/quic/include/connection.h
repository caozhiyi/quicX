#ifndef QUIC_INCLUDE_CONNECTION
#define QUIC_INCLUDE_CONNECTION

#include <memory>
#include <string>
#include "quic/include/type.h"
#include "quic/include/stream.h"

namespace quicx {
namespace quic {

class QuicxConnection {
public:
    QuicxConnection() {}
    virtual ~QuicxConnection() {}

    virtual std::shared_ptr<QuicxStream> MakeStream(StreamType st) = 0;

    virtual void SetStreamStateCallBack(stream_state_call_back cb) = 0;

    virtual void Close(uint64_t error = 0) = 0;

    virtual void GetLocalAddr(std::string& addr, uint32_t& port) = 0;
    virtual void GetRemoteAddr(std::string& addr, uint32_t& port) = 0;

    void SetUserData(void* user_data) { _user_data = user_data; }
    void* GetUserData() { return _user_data; }
private:
    void* _user_data;
};

}
}

#endif