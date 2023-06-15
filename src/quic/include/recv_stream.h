#ifndef QUIC_INCLUDE_RECV_STREAM
#define QUIC_INCLUDE_RECV_STREAM

#include <string>
#include <cstdint>

namespace quicx {

template <typename T = int>
class RecvStream<T> {
public:
    RecvStream() {}
    virtual ~RecvStream() {}

    virtual void Close() = 0;

    virtual uint64_t GetStreamID() = 0;

    void SetUserData(T user_data) { _user_data = user_data; }
    T GetUserData() { return _user_data; }
private:
    T _user_data;
};

}

#endif