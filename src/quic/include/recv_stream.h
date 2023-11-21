#ifndef QUIC_INCLUDE_RECV_STREAM
#define QUIC_INCLUDE_RECV_STREAM

#include <string>
#include <cstdint>

namespace quicx {
namespace quic {

class RecvStream {
public:
    RecvStream() {}
    virtual ~RecvStream() {}

    virtual void Close() = 0;

    virtual uint64_t GetStreamID() = 0;

    void SetUserData(void* user_data) { _user_data = user_data; }
    void* GetUserData() { return _user_data; }
private:
    void* _user_data;
};

}
}

#endif