#ifndef QUIC_INCLUDE_SEND_STREAM
#define QUIC_INCLUDE_SEND_STREAM

#include <string>
#include <cstdint>

namespace quicx {

template <typename T = int>
class SendStream<T> {
public:
    SendStream() {}
    virtual ~SendStream() {}

    virtual int32_t Send(const std::string& data);
    virtual int32_t Send(u_char* data, uint32_t len);

    virtual void Close() = 0;

    virtual void Reset(uint64_t error = 0) = 0;

    virtual uint64_t GetStreamID() = 0;

    void SetUserData(T user_data) { _user_data = user_data; }
    T GetUserData() { return _user_data; }
private:
    T _user_data;
};

}

#endif